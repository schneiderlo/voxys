// Compile ordinary and authored pairs separately to bound the per-kernel
// shader compiler workload. Each pair is processed by exactly one pass.
override AUTHORED_PAIR_PASS: bool = false;

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
// Exact exterior queries in authored root space. Included after the immutable
// heap accessors. Feature ordinals resolve through the same retained shape.
struct AuthoredQuerySurface {
    valid: bool, distance: f32, point: vec3<f32>, normal: vec3<f32>,
    feature: u32, source: u32,
};
struct AuthoredSegmentClosest { squared: f32, query: vec3<f32>, surface: vec3<f32>, };

// Squared distance from a segment to an axis-aligned closed box/rectangle.
// Clamp changes polynomial only at the six slab boundaries. Minimize each
// quadratic interval exactly; sampling can miss a thin crane/rail between taps.
fn authored_segment_box(a: vec3<f32>, b: vec3<f32>, lo: vec3<f32>, hi: vec3<f32>) -> AuthoredSegmentClosest {
    let v = b-a;
    var cuts: array<f32,8>;
    cuts[0] = 0.0; cuts[1] = 1.0;
    var count = 2u;
    for (var axis=0u; axis<3u; axis++) {
        if (abs(v[axis]) <= 1e-20) { continue; }
        let first = (lo[axis]-a[axis])/v[axis];
        let last = (hi[axis]-a[axis])/v[axis];
        if (first>0.0 && first<1.0) { cuts[count]=first; count++; }
        if (last>0.0 && last<1.0) { cuts[count]=last; count++; }
    }
    for (var i=1u; i<count; i++) {
        let value=cuts[i]; var at=i;
        while (at>0u) { if (cuts[at-1u]<=value) { break; } cuts[at]=cuts[at-1u]; at--; }
        cuts[at]=value;
    }
    let initial=clamp(a,lo,hi);
    var best=AuthoredSegmentClosest(dot(a-initial,a-initial),a,initial);
    for (var i=0u; i+1u<count; i++) {
        let midpoint=a+v*(.5*(cuts[i]+cuts[i+1u]));
        var numerator=0.0; var denominator=0.0;
        for (var axis=0u; axis<3u; axis++) {
            if (midpoint[axis]<lo[axis] || midpoint[axis]>hi[axis] || lo[axis]==hi[axis]) {
                let bound=select(hi[axis],lo[axis],midpoint[axis]<=lo[axis]);
                numerator+=v[axis]*(a[axis]-bound); denominator+=v[axis]*v[axis];
            }
        }
        // A flat minimum spans the interval. Its midpoint avoids cancellation
        // at a slab edge on long segments (and an artificial sideways normal).
        var t=.5*(cuts[i]+cuts[i+1u]);
        if (denominator>1e-30) { t=clamp(-numerator/denominator,cuts[i],cuts[i+1u]); }
        let query=a+v*t; let surface=clamp(query,lo,hi); let delta=query-surface;
        let squared=dot(delta,delta);
        if (squared<best.squared) { best=AuthoredSegmentClosest(squared,query,surface); }
    }
    return best;
}

fn authored_ray_interval(lo: vec3<f32>, hi: vec3<f32>, origin: vec3<f32>, direction: vec3<f32>, maximum: f32) -> vec2<f32> {
    var interval=vec2<f32>(0.0,maximum);
    for (var axis=0u; axis<3u; axis++) {
        if (abs(direction[axis])<1e-20) {
            if (origin[axis]<lo[axis] || origin[axis]>hi[axis]) { return vec2<f32>(1.0,-1.0); }
        } else {
            let a=(lo[axis]-origin[axis])/direction[axis];
            let b=(hi[axis]-origin[axis])/direction[axis];
            interval.x=max(interval.x,min(a,b)); interval.y=min(interval.y,max(a,b));
            if (interval.x>interval.y) { return vec2<f32>(1.0,-1.0); }
        }
    }
    return interval;
}

fn authored_segment_surface(shape: AuthoredShapeView, a: vec3<f32>, b: vec3<f32>) -> AuthoredQuerySurface {
    var result=AuthoredQuerySurface(false,1e30,a,vec3<f32>(0,1,0),0xffffffffu,0u);
    if (!shape.valid) { return result; }
    var squared=1e30; var inside=false; var closest=a;
    var nodeIndex=0u;
    while (nodeIndex<shape.node_count) {
        let node=authored_node(shape,nodeIndex);
        if (!node.valid) { result.valid=false; return result; }
        let lower=authored_segment_box(a,b,node.minimum,node.maximum);
        if (lower.squared>squared) { nodeIndex=node.escape; continue; }
        nodeIndex++;
        if (node.cell==0xffffffffu) { continue; }
        let cell=authored_cell(shape,node.cell);
        if (!cell.valid) { result.valid=false; return result; }
        let segmentRange=authored_ray_interval(cell.minimum,cell.maximum,a,b-a,1.0);
        inside=inside || segmentRange.x<=segmentRange.y;
        for (var i=0u; i<cell.face_count; i++) {
            let index=cell.first_face+i; let face=authored_face(shape,index);
            if (!face.valid) { result.valid=false; return result; }
            let candidate=authored_segment_box(a,b,face.minimum,face.maximum);
            if (candidate.squared<squared || (candidate.squared==squared && index<result.feature)) {
                squared=candidate.squared; closest=candidate.query;
                result.point=candidate.surface; result.feature=index; result.source=face.source;
                result.normal=vec3<f32>(0); result.normal[face.axis]=f32(face.sign);
                result.valid=true;
            }
        }
    }
    if (result.valid) {
        let distance=sqrt(squared);
        result.distance=select(distance,-distance,inside);
        if (distance>1e-7) { result.normal=(closest-result.point)/result.distance; }
        result.feature |= 0x80000000u;
    }
    return result;
}

struct AuthoredQueryRay { valid: bool, distance: f32, normal: vec3<f32>, feature: u32, source: u32, };
fn authored_ray(shape: AuthoredShapeView, origin: vec3<f32>, direction: vec3<f32>, maximum: f32) -> AuthoredQueryRay {
    var result=AuthoredQueryRay(false,maximum,vec3<f32>(0,1,0),0xffffffffu,0u);
    if (!shape.valid) { return result; }
    let start=authored_segment_surface(shape,origin,origin);
    if (start.valid && start.distance<=0.0) {
        return AuthoredQueryRay(true,0.0,start.normal,start.feature,start.source);
    }
    var nodeIndex=0u;
    while (nodeIndex<shape.node_count) {
        let node=authored_node(shape,nodeIndex);
        if (!node.valid) { result.valid=false; return result; }
        let range=authored_ray_interval(node.minimum,node.maximum,origin,direction,result.distance);
        if (range.x>range.y) { nodeIndex=node.escape; continue; }
        nodeIndex++;
        if (node.cell==0xffffffffu) { continue; }
        let cell=authored_cell(shape,node.cell);
        if (!cell.valid) { result.valid=false; return result; }
        for (var i=0u; i<cell.face_count; i++) {
            let index=cell.first_face+i; let face=authored_face(shape,index);
            if (!face.valid) { result.valid=false; return result; }
            if (abs(direction[face.axis])<1e-20) { continue; }
            let distance=(face.minimum[face.axis]-origin[face.axis])/direction[face.axis];
            if (distance<0.0 || distance>result.distance) { continue; }
            let point=origin+direction*distance;
            let u=(face.axis+1u)%3u; let v=(face.axis+2u)%3u;
            if (point[u]<face.minimum[u]-1e-6 || point[u]>face.maximum[u]+1e-6
                || point[v]<face.minimum[v]-1e-6 || point[v]>face.maximum[v]+1e-6) { continue; }
            let feature=index|0x80000000u;
            if (result.valid && distance==result.distance && feature>=result.feature) { continue; }
            var normal=vec3<f32>(0); normal[face.axis]=f32(face.sign);
            result=AuthoredQueryRay(true,distance,normal,feature,face.source);
        }
    }
    return result;
}
// END GENERATED AUTHORED GEOMETRY

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

const SHAPE_SPHERE : u32 = 0u;
const SHAPE_CUBE : u32 = 1u;
const SHAPE_BOX : u32 = 2u;
const SHAPE_CAPSULE : u32 = 3u;
const SHAPE_CYLINDER : u32 = 4u;
const PAIR_CLASS_COUNT : u32 = 10u;
const MAX_CANDIDATES : u32 = 16u;
const SENTINEL : u32 = 0xffffffffu;
const BODY_AWAKE : u32 = 1u << 21u;
// Must match physics_ballistic.wgsl. The fractional shape-type payload is a
// conservative one-tick linear travel bound for tunnelling-risk bodies.
const SHAPE_SWEEP_RANGE : f32 = 256.0;

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

struct KeyValue {
    keyLow : u32,
    keyHigh : u32,
    value : u32,
    ordinal : u32,
};

struct ManifoldPoint {
    localAnchorA_separation : vec4<f32>,
    localAnchorB_normalImpulse : vec4<f32>,
    features : vec4<u32>,
    impulses : vec4<f32>,
};

struct ContactManifold {
    pair : KeyValue,
    state : vec4<u32>,
    normal : vec4<f32>,
    tangent1 : vec4<f32>,
    tangent2 : vec4<f32>,
    frictionAnchorA : vec4<f32>,
    frictionAnchorB : vec4<f32>,
    rollingImpulse : vec4<f32>,
    points : array<ManifoldPoint, 4>,
};

struct NarrowParams {
    capacities : vec4<u32>,
    tolerances : vec4<f32>,
    dispatch : vec4<u32>,
};

struct ContactCandidate {
    pointA_separation : vec4<f32>,
    pointB : vec4<f32>,
    features : vec4<u32>,
};

struct CandidateSet {
    items : array<ContactCandidate, 16>,
    normal : vec3<f32>,
    count : u32,
};

struct ReducedCandidateSet {
    items : array<ContactCandidate, 4>,
    normal : vec3<f32>,
    count : u32,
};

struct Segment {
    first : vec3<f32>,
    second : vec3<f32>,
};

struct ClosestSegments {
    pointA : vec3<f32>,
    pointB : vec3<f32>,
    fractionA : f32,
    fractionB : f32,
};

struct BoxFrame {
    center : vec3<f32>,
    axisX : vec3<f32>,
    axisY : vec3<f32>,
    axisZ : vec3<f32>,
    half : vec3<f32>,
};

struct PolyFrame {
    center : vec3<f32>,
    orientation : vec4<f32>,
    dimensions : vec3<f32>,
};

struct ClosestSurface {
    point : vec3<f32>,
    normalToShape : vec3<f32>,
    signedDistance : f32,
    feature : u32,
};

struct SatResult {
    normal : vec3<f32>,
    separation : f32,
    axisKind : u32,
    axisA : u32,
    axisB : u32,
    valid : u32,
};

struct ClipPoint {
    position : vec3<f32>,
    feature : u32,
};

struct ClipPolygon {
    points : array<ClipPoint, 8>,
    count : u32,
};

@group(0) @binding(0) var<storage, read> poses : array<BodyPose>;
@group(0) @binding(1) var<storage, read> shapes : array<BodyShape>;
@group(0) @binding(2) var<storage, read> uniquePairs : array<KeyValue>;
@group(0) @binding(3) var<storage, read> broadTelemetry : array<u32>;
@group(0) @binding(4) var<storage, read_write> bucketedPairs : array<KeyValue>;
@group(0) @binding(5) var<storage, read_write> classTable : array<atomic<u32>>;
@group(0) @binding(6) var<storage, read> previousManifolds : array<ContactManifold>;
@group(0) @binding(7) var<storage, read_write> currentManifolds : array<ContactManifold>;
@group(0) @binding(8) var<storage, read> metadata : array<vec4<i32>>;
@group(0) @binding(9) var<storage, read_write> narrowTelemetry : array<atomic<u32>>;
@group(0) @binding(10) var<uniform> narrow : NarrowParams;
@group(0) @binding(11) var<storage, read_write> classDispatchArgs : array<u32>;
@group(0) @binding(12) var<storage, read_write> activeManifolds : array<ContactManifold>;
@group(0) @binding(13) var<storage, read_write> activePredicates : array<u32>;
@group(0) @binding(14) var<storage, read> activeOffsets : array<u32>;
@group(0) @binding(15) var<storage, read> authored_shape_heap : array<vec4<u32>>;
struct CollisionClasses { words: array<vec4<u32>, 8>, };
@group(0) @binding(16) var<uniform> collisionClasses: CollisionClasses;

var<private> currentSpeculativeDistance : f32 = 0.0;

// Per-invocation subshape views. Parent poses are restored before anchors
// are built, so solver impulses always act on the complete rigid brick.
var<private> compoundEnabled: bool = false;
var<private> compoundA: u32;
var<private> compoundB: u32;
var<private> compoundPoseA: BodyPose;
var<private> compoundPoseB: BodyPose;
var<private> compoundShapeA: BodyShape;
var<private> compoundShapeB: BodyShape;
fn compound_pose(body:u32)->BodyPose {
    if(compoundEnabled){if(body==compoundA){return compoundPoseA;}if(body==compoundB){return compoundPoseB;}}
    return poses[body];
}
fn compound_shape(body:u32)->BodyShape {
    if(compoundEnabled){if(body==compoundA){return compoundShapeA;}if(body==compoundB){return compoundShapeB;}}
    return shapes[body];
}

fn canonical_shape(shapeValue : f32) -> u32 {
    let shapeType = u32(clamp(shapeValue, 0.0, 4.0));
    if (shapeType == SHAPE_SPHERE) { return 0u; }
    if (shapeType == SHAPE_CAPSULE) { return 1u; }
    if (shapeType == SHAPE_CYLINDER) { return 3u; }
    return 2u;
}

fn pair_class(shapeA : u32, shapeB : u32) -> u32 {
    let low = min(shapeA, shapeB);
    let high = max(shapeA, shapeB);
    if (low == 0u && high == 0u) { return 0u; }
    if (low == 0u && high == 1u) { return 1u; }
    if (low == 1u && high == 1u) { return 2u; }
    if (low == 0u && high == 2u) { return 3u; }
    if (low == 1u && high == 2u) { return 4u; }
    if (low == 2u && high == 2u) { return 5u; }
    if (low == 0u && high == 3u) { return 6u; }
    if (low == 1u && high == 3u) { return 7u; }
    if (low == 2u && high == 3u) { return 8u; }
    return 9u;
}

fn empty_manifold() -> ContactManifold {
    var result : ContactManifold;
    result.pair = KeyValue(SENTINEL, SENTINEL, 0u, 0u);
    result.state = vec4<u32>(0u);
    result.normal = vec4<f32>(0.0);
    result.tangent1 = vec4<f32>(0.0);
    result.tangent2 = vec4<f32>(0.0);
    result.frictionAnchorA = vec4<f32>(0.0);
    result.frictionAnchorB = vec4<f32>(0.0);
    result.rollingImpulse = vec4<f32>(0.0);
    for (var point = 0u; point < 4u; point += 1u) {
        result.points[point].localAnchorA_separation = vec4<f32>(0.0);
        result.points[point].localAnchorB_normalImpulse = vec4<f32>(0.0);
        result.points[point].features = vec4<u32>(0u);
        result.points[point].impulses = vec4<f32>(0.0);
    }
    return result;
}

@compute @workgroup_size(1)
fn reset_pair_buckets(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    for (var word = 0u; word < 18u; word += 1u) {
        atomicStore(&narrowTelemetry[word], 0u);
    }
    for (var word = 0u; word < 30u; word += 1u) {
        atomicStore(&classTable[word], 0u);
    }
}

fn reported_pair_count() -> u32 {
    let reportedCount = broadTelemetry[3];
    return min(reportedCount,
        min(narrow.capacities.y, narrow.capacities.z));
}

fn solver_pair_count() -> u32 {
    return min(atomicLoad(&narrowTelemetry[10]),
        min(narrow.dispatch.x, narrow.capacities.z));
}

fn mark_active_manifolds_impl(gid : vec3<u32>) {
    let pairIndex = gid.x;
    if (pairIndex >= narrow.dispatch.x) { return; }
    activePredicates[pairIndex] = select(0u, 1u,
        pairIndex < solver_pair_count()
        && currentManifolds[pairIndex].state.x != 0u);
}

fn scatter_active_manifolds_impl(gid : vec3<u32>) {
    let pairIndex = gid.x;
    if (pairIndex >= narrow.dispatch.x
        || activePredicates[pairIndex] == 0u) { return; }
    activeManifolds[activeOffsets[pairIndex]] = currentManifolds[pairIndex];
}

fn commit_active_manifolds_impl(gid : vec3<u32>) {
    let rank = gid.x;
    if (rank >= atomicLoad(&narrowTelemetry[24])) { return; }
    let manifold = activeManifolds[rank];
    if (manifold.pair.ordinal < solver_pair_count()) {
        currentManifolds[manifold.pair.ordinal] = manifold;
    }
}

@compute @workgroup_size(1)
fn finalize_active_manifolds(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    let inputCount = solver_pair_count();
    var count = 0u;
    if (inputCount != 0u) {
        let last = inputCount - 1u;
        count = activeOffsets[last] + activePredicates[last];
    }
    atomicStore(&narrowTelemetry[24], count);
    let activeDispatch = PAIR_CLASS_COUNT * 4u;
    classDispatchArgs[activeDispatch] =
        (count + narrow.capacities.w - 1u) / narrow.capacities.w;
    classDispatchArgs[activeDispatch + 1u] = 1u;
    classDispatchArgs[activeDispatch + 2u] = 1u;
    classDispatchArgs[activeDispatch + 3u] = 0u;
}

@compute @workgroup_size(64)
fn mark_active_manifolds_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_active_manifolds_impl(gid);
}
@compute @workgroup_size(128)
fn mark_active_manifolds_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_active_manifolds_impl(gid);
}
@compute @workgroup_size(256)
fn mark_active_manifolds_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_active_manifolds_impl(gid);
}

@compute @workgroup_size(64)
fn scatter_active_manifolds_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_active_manifolds_impl(gid);
}
@compute @workgroup_size(128)
fn scatter_active_manifolds_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_active_manifolds_impl(gid);
}
@compute @workgroup_size(256)
fn scatter_active_manifolds_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_active_manifolds_impl(gid);
}

@compute @workgroup_size(64)
fn commit_active_manifolds_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    commit_active_manifolds_impl(gid);
}
@compute @workgroup_size(128)
fn commit_active_manifolds_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    commit_active_manifolds_impl(gid);
}
@compute @workgroup_size(256)
fn commit_active_manifolds_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    commit_active_manifolds_impl(gid);
}

fn pair_class_for_record(pair : KeyValue) -> u32 {
    let shapeA = select(canonical_shape(shapes[pair.keyHigh].dimensions_type.w),
        2u, any(shapes[pair.keyHigh].authored_shape != vec4<u32>(0u)));
    let shapeB = select(canonical_shape(shapes[pair.keyLow].dimensions_type.w),
        2u, any(shapes[pair.keyLow].authored_shape != vec4<u32>(0u)));
    return pair_class(shapeA, shapeB);
}

fn count_pair_classes_impl(gid : vec3<u32>) {
    let pairIndex = gid.x;
    if (pairIndex >= reported_pair_count()) { return; }
    let pair = uniquePairs[pairIndex];
    if (pair.keyHigh >= narrow.capacities.x
        || pair.keyLow >= narrow.capacities.x) { return; }
    let pairClass = pair_class_for_record(pair);
    atomicAdd(&narrowTelemetry[pairClass], 1u);
    if (!bounding_spheres_may_touch(pair.keyHigh, pair.keyLow)) {
        let pairRecord = KeyValue(pair.keyLow, pair.keyHigh,
            pairClass | ((pair.value & 1u) << 8u), pairIndex);
        currentManifolds[pairIndex] = base_manifold(pairRecord, 0u);
        return;
    }
    atomicAdd(&classTable[pairClass], 1u);
}

@compute @workgroup_size(1)
fn finalize_pair_buckets(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    var prefix = 0u;
    for (var pairClass = 0u; pairClass < PAIR_CLASS_COUNT;
         pairClass += 1u) {
        let count = atomicLoad(&classTable[pairClass]);
        atomicStore(&classTable[PAIR_CLASS_COUNT + pairClass], prefix);
        atomicStore(&classTable[2u * PAIR_CLASS_COUNT + pairClass], prefix);
        let dispatch = pairClass * 4u;
        classDispatchArgs[dispatch] =
            (count + narrow.capacities.w - 1u) / narrow.capacities.w;
        classDispatchArgs[dispatch + 1u] = 1u;
        classDispatchArgs[dispatch + 2u] = 1u;
        classDispatchArgs[dispatch + 3u] = 0u;
        prefix += count;
    }
    let activeContacts = min(reported_pair_count(), narrow.dispatch.x);
    let activeDispatch = PAIR_CLASS_COUNT * 4u;
    classDispatchArgs[activeDispatch] =
        (activeContacts + narrow.capacities.w - 1u) / narrow.capacities.w;
    classDispatchArgs[activeDispatch + 1u] = 1u;
    classDispatchArgs[activeDispatch + 2u] = 1u;
    classDispatchArgs[activeDispatch + 3u] = 0u;
    let reportedCount = broadTelemetry[3];
    atomicStore(&narrowTelemetry[10], reportedCount);
    atomicStore(&narrowTelemetry[17], select(0u, 1u,
        reportedCount > narrow.capacities.y
        || reportedCount > narrow.capacities.z));
    atomicMax(&narrowTelemetry[18], reportedCount);
}

fn scatter_pair_classes_impl(gid : vec3<u32>) {
    let pairIndex = gid.x;
    if (pairIndex >= reported_pair_count()) { return; }
    let pair = uniquePairs[pairIndex];
    if (pair.keyHigh >= narrow.capacities.x
        || pair.keyLow >= narrow.capacities.x) { return; }
    if (!bounding_spheres_may_touch(pair.keyHigh, pair.keyLow)) { return; }
    let pairClass = pair_class_for_record(pair);
    let bucketIndex = atomicAdd(
        &classTable[2u * PAIR_CLASS_COUNT + pairClass], 1u);
    bucketedPairs[bucketIndex] = KeyValue(
        pair.keyLow, pair.keyHigh,
        pairClass | ((pair.value & 1u) << 8u), pairIndex);
}

@compute @workgroup_size(64)
fn count_pair_classes_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    count_pair_classes_impl(gid);
}
@compute @workgroup_size(128)
fn count_pair_classes_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    count_pair_classes_impl(gid);
}
@compute @workgroup_size(256)
fn count_pair_classes_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    count_pair_classes_impl(gid);
}

@compute @workgroup_size(64)
fn scatter_pair_classes_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_pair_classes_impl(gid);
}
@compute @workgroup_size(128)
fn scatter_pair_classes_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_pair_classes_impl(gid);
}
@compute @workgroup_size(256)
fn scatter_pair_classes_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_pair_classes_impl(gid);
}

fn quaternion_rotate(q : vec4<f32>, value : vec3<f32>) -> vec3<f32> {
    let twiceCross = 2.0 * cross(q.xyz, value);
    return value + q.w * twiceCross + cross(q.xyz, twiceCross);
}

fn quaternion_inverse_rotate(q : vec4<f32>, value : vec3<f32>) -> vec3<f32> {
    return quaternion_rotate(vec4<f32>(-q.xyz, q.w), value);
}

fn safe_normalize(value : vec3<f32>, fallback : vec3<f32>) -> vec3<f32> {
    let squared = dot(value, value);
    if (squared > 1e-14 && squared < 1e30) {
        return value * inverseSqrt(squared);
    }
    return fallback;
}

fn axis_value(frame : BoxFrame, index : u32) -> vec3<f32> {
    if (index == 0u) { return frame.axisX; }
    if (index == 1u) { return frame.axisY; }
    return frame.axisZ;
}

fn component_value(value : vec3<f32>, index : u32) -> f32 {
    if (index == 0u) { return value.x; }
    if (index == 1u) { return value.y; }
    return value.z;
}

fn adjacent_sector_delta(reference : i32, other : i32) -> i32 {
    if (other == reference) { return 0; }
    if (reference < 2147483647 && other == reference + 1) { return 1; }
    if (reference > -2147483647 - 1 && other == reference - 1) { return -1; }
    return 0x3fffffff;
}

fn body_position_in_frame(body : u32, frameBody : u32) -> vec3<f32> {
    let position = compound_pose(body).position_invMass.xyz;
    let frameSector = metadata[frameBody].xyz;
    let bodySector = metadata[body].xyz;
    var sectorDelta = vec3<i32>(0);
    if (!all(frameSector == bodySector)) {
        for (var axis = 0u; axis < 3u; axis += 1u) {
            sectorDelta[axis] = adjacent_sector_delta(
                frameSector[axis], bodySector[axis]);
            if (abs(sectorDelta[axis]) > 1) {
                return vec3<f32>(3.402823466e+38);
            }
        }
    }
    return position + vec3<f32>(sectorDelta) * 256.0;
}

fn make_box_at_center(body : u32, center : vec3<f32>) -> BoxFrame {
    let pose = compound_pose(body);
    var result : BoxFrame;
    result.center = center;
    result.axisX = quaternion_rotate(
        pose.orientation, vec3<f32>(1.0, 0.0, 0.0));
    result.axisY = quaternion_rotate(
        pose.orientation, vec3<f32>(0.0, 1.0, 0.0));
    result.axisZ = quaternion_rotate(
        pose.orientation, vec3<f32>(0.0, 0.0, 1.0));
    result.half = 0.5 * max(abs(compound_shape(body).dimensions_type.xyz),
                            vec3<f32>(1e-5));
    return result;
}

fn make_box(body : u32, frameBody : u32) -> BoxFrame {
    return make_box_at_center(
        body, body_position_in_frame(body, frameBody));
}

fn has_identity_rotation(body : u32) -> bool {
    let orientation = compound_pose(body).orientation;
    return all(orientation.xyz == vec3<f32>(0.0))
        && abs(orientation.w) == 1.0;
}

fn make_axis_aligned_box(body : u32, frameBody : u32) -> BoxFrame {
    var result : BoxFrame;
    result.center = body_position_in_frame(body, frameBody);
    result.axisX = vec3<f32>(1.0, 0.0, 0.0);
    result.axisY = vec3<f32>(0.0, 1.0, 0.0);
    result.axisZ = vec3<f32>(0.0, 0.0, 1.0);
    result.half = 0.5 * max(abs(compound_shape(body).dimensions_type.xyz),
                            vec3<f32>(1e-5));
    return result;
}

fn make_poly_frame(body : u32, center : vec3<f32>) -> PolyFrame {
    var result : PolyFrame;
    result.center = center;
    result.orientation = compound_pose(body).orientation;
    result.dimensions = compound_shape(body).dimensions_type.xyz;
    return result;
}

fn make_box_from_poly(frame : PolyFrame) -> BoxFrame {
    var result : BoxFrame;
    result.center = frame.center;
    result.axisX = quaternion_rotate(
        frame.orientation, vec3<f32>(1.0, 0.0, 0.0));
    result.axisY = quaternion_rotate(
        frame.orientation, vec3<f32>(0.0, 1.0, 0.0));
    result.axisZ = quaternion_rotate(
        frame.orientation, vec3<f32>(0.0, 0.0, 1.0));
    result.half = 0.5 * max(abs(frame.dimensions), vec3<f32>(1e-5));
    return result;
}

fn sphere_radius(body : u32) -> f32 {
    return 0.5 * max(abs(compound_shape(body).dimensions_type.x), 1e-5);
}

fn capsule_radius(body : u32) -> f32 {
    let dimensions = max(abs(compound_shape(body).dimensions_type.xyz),
                         vec3<f32>(1e-5));
    return 0.25 * (dimensions.x + dimensions.z);
}

fn capsule_segment(body : u32, frameBody : u32) -> Segment {
    let radius = capsule_radius(body);
    let halfSegment = max(0.5 * abs(compound_shape(body).dimensions_type.y)
                              - radius, 0.0);
    let offset = quaternion_rotate(
        compound_pose(body).orientation, vec3<f32>(0.0, halfSegment, 0.0));
    let center = body_position_in_frame(body, frameBody);
    return Segment(center - offset, center + offset);
}

fn cylinder_radius(body : u32) -> f32 {
    let dimensions = max(abs(compound_shape(body).dimensions_type.xyz),
                         vec3<f32>(1e-5));
    return 0.25 * (dimensions.x + dimensions.z);
}

fn cylinder_half_height(body : u32) -> f32 {
    return 0.5 * max(abs(compound_shape(body).dimensions_type.y), 1e-5);
}

fn shape_bounding_radius(body : u32) -> f32 {
    let dimensions = max(abs(compound_shape(body).dimensions_type.xyz),
                         vec3<f32>(1e-5));
    let shapeClass = canonical_shape(compound_shape(body).dimensions_type.w);
    if (shapeClass == 0u) { return 0.5 * dimensions.x; }
    if (shapeClass == 1u) {
        let radius = 0.25 * (dimensions.x + dimensions.z);
        return max(0.5 * dimensions.y - radius, 0.0) + radius;
    }
    if (shapeClass == 3u) {
        let radius = 0.25 * (dimensions.x + dimensions.z);
        return length(vec2<f32>(radius, 0.5 * dimensions.y));
    }
    return 0.5 * length(dimensions);
}

fn shape_sweep_distance(body : u32) -> f32 {
    if ((u32(metadata[body].w) & BODY_AWAKE) == 0u) { return 0.0; }
    return fract(max(compound_shape(body).dimensions_type.w, 0.0))
        * SHAPE_SWEEP_RANGE;
}

fn pair_speculative_distance(bodyA : u32, bodyB : u32) -> f32 {
    return narrow.tolerances.y
        + shape_sweep_distance(bodyA) + shape_sweep_distance(bodyB);
}

fn bounding_spheres_may_touch(bodyA : u32, bodyB : u32) -> bool {
    let delta = body_position_in_frame(bodyB, bodyA)
              - body_position_in_frame(bodyA, bodyA);
    let reach = shape_bounding_radius(bodyA)
              + shape_bounding_radius(bodyB)
              + pair_speculative_distance(bodyA, bodyB);
    return dot(delta, delta) <= reach * reach;
}

fn closest_point_segment(point : vec3<f32>, segment : Segment) -> vec4<f32> {
    let edge = segment.second - segment.first;
    let denominator = dot(edge, edge);
    let fraction = select(0.0, clamp(
        dot(point - segment.first, edge) / denominator, 0.0, 1.0),
        denominator > 1e-12);
    return vec4<f32>(segment.first + fraction * edge, fraction);
}

fn closest_segments(a : Segment, b : Segment) -> ClosestSegments {
    let directionA = a.second - a.first;
    let directionB = b.second - b.first;
    let offset = a.first - b.first;
    let aa = dot(directionA, directionA);
    let bb = dot(directionB, directionB);
    let ab = dot(directionA, directionB);
    let ao = dot(directionA, offset);
    let bo = dot(directionB, offset);
    let denominator = aa * bb - ab * ab;
    var fractionA = 0.0;
    if (aa > 1e-12 && denominator > 1e-12) {
        fractionA = clamp((ab * bo - bb * ao) / denominator, 0.0, 1.0);
    }
    var fractionB = 0.0;
    if (bb > 1e-12) {
        fractionB = clamp((ab * fractionA + bo) / bb, 0.0, 1.0);
    }
    if (aa > 1e-12) {
        fractionA = clamp((ab * fractionB - ao) / aa, 0.0, 1.0);
    }
    var result : ClosestSegments;
    result.fractionA = fractionA;
    result.fractionB = fractionB;
    result.pointA = a.first + fractionA * directionA;
    result.pointB = b.first + fractionB * directionB;
    return result;
}

fn feature_from_fraction(fraction : f32, base : u32) -> u32 {
    if (fraction <= 1e-4) { return base; }
    if (fraction >= 0.9999) { return base + 1u; }
    return base + 2u;
}

fn append_candidate(candidateSet : ptr<function, CandidateSet>,
                    pointA : vec3<f32>, pointB : vec3<f32>, separation : f32,
                    featureA : u32, featureB : u32) {
    if ((*candidateSet).count >= MAX_CANDIDATES
        || separation > currentSpeculativeDistance
        || any(abs(pointA) > vec3<f32>(1e15))
        || any(abs(pointB) > vec3<f32>(1e15))) { return; }
    let slot = (*candidateSet).count;
    (*candidateSet).items[slot].pointA_separation = vec4<f32>(pointA, separation);
    (*candidateSet).items[slot].pointB = vec4<f32>(pointB, 0.0);
    (*candidateSet).items[slot].features = vec4<u32>(
        featureA, featureB, 0u, 0u);
    (*candidateSet).count = slot + 1u;
}

fn swap_candidates(source : CandidateSet) -> CandidateSet {
    var localSource = source;
    var result = localSource;
    result.normal = -localSource.normal;
    for (var index = 0u; index < localSource.count; index += 1u) {
        result.items[index].pointA_separation = vec4<f32>(
            localSource.items[index].pointB.xyz,
            localSource.items[index].pointA_separation.w);
        result.items[index].pointB = vec4<f32>(
            localSource.items[index].pointA_separation.xyz, 0.0);
        result.items[index].features = vec4<u32>(
            localSource.items[index].features.y,
            localSource.items[index].features.x, 0u, 0u);
    }
    return result;
}

fn empty_candidates() -> CandidateSet {
    var result : CandidateSet;
    result.count = 0u;
    result.normal = vec3<f32>(1.0, 0.0, 0.0);
    return result;
}

fn empty_reduced_candidates() -> ReducedCandidateSet {
    var result : ReducedCandidateSet;
    result.normal = vec3<f32>(1.0, 0.0, 0.0);
    result.count = 0u;
    return result;
}

fn collide_sphere_sphere(bodyA : u32, bodyB : u32,
                         frameBody : u32) -> CandidateSet {
    var result = empty_candidates();
    let centerA = body_position_in_frame(bodyA, frameBody);
    let centerB = body_position_in_frame(bodyB, frameBody);
    let delta = centerB - centerA;
    let distance = length(delta);
    let normal = safe_normalize(delta, vec3<f32>(1.0, 0.0, 0.0));
    let radiusA = sphere_radius(bodyA);
    let radiusB = sphere_radius(bodyB);
    let separation = distance - radiusA - radiusB;
    result.normal = normal;
    append_candidate(&result, centerA + normal * radiusA,
                      centerB - normal * radiusB, separation, 0u, 0u);
    return result;
}

fn collide_sphere_capsule(sphereBody : u32,
                          capsuleBody : u32,
                          frameBody : u32) -> CandidateSet {
    var result = empty_candidates();
    let sphereCenter = body_position_in_frame(sphereBody, frameBody);
    let closest = closest_point_segment(
        sphereCenter, capsule_segment(capsuleBody, frameBody));
    let delta = closest.xyz - sphereCenter;
    let distance = length(delta);
    let normal = safe_normalize(delta, safe_normalize(
        body_position_in_frame(capsuleBody, frameBody) - sphereCenter,
        vec3<f32>(1.0, 0.0, 0.0)));
    let sphereRadius = sphere_radius(sphereBody);
    let capsuleRadiusValue = capsule_radius(capsuleBody);
    let separation = distance - sphereRadius - capsuleRadiusValue;
    result.normal = normal;
    append_candidate(&result, sphereCenter + normal * sphereRadius,
                      closest.xyz - normal * capsuleRadiusValue,
                      separation, 0u,
                      feature_from_fraction(closest.w, 0x100u));
    return result;
}

fn collide_capsule_capsule(bodyA : u32, bodyB : u32,
                           frameBody : u32) -> CandidateSet {
    var result = empty_candidates();
    let closest = closest_segments(
        capsule_segment(bodyA, frameBody), capsule_segment(bodyB, frameBody));
    let delta = closest.pointB - closest.pointA;
    let distance = length(delta);
    let normal = safe_normalize(delta, safe_normalize(
        body_position_in_frame(bodyB, frameBody)
            - body_position_in_frame(bodyA, frameBody),
        vec3<f32>(1.0, 0.0, 0.0)));
    let radiusA = capsule_radius(bodyA);
    let radiusB = capsule_radius(bodyB);
    result.normal = normal;
    append_candidate(&result,
        closest.pointA + normal * radiusA,
        closest.pointB - normal * radiusB,
        distance - radiusA - radiusB,
        feature_from_fraction(closest.fractionA, 0x100u),
        feature_from_fraction(closest.fractionB, 0x100u));
    return result;
}

fn box_surface(frame : BoxFrame, point : vec3<f32>) -> ClosestSurface {
    let relative = point - frame.center;
    let local = vec3<f32>(dot(relative, frame.axisX),
                          dot(relative, frame.axisY),
                          dot(relative, frame.axisZ));
    var closestLocal = clamp(local, -frame.half, frame.half);
    let outsideDelta = closestLocal - local;
    let outsideSquared = dot(outsideDelta, outsideDelta);
    var result : ClosestSurface;
    if (outsideSquared > 1e-14) {
        let distance = sqrt(outsideSquared);
        let localNormal = outsideDelta / distance;
        result.point = frame.center + frame.axisX * closestLocal.x
            + frame.axisY * closestLocal.y + frame.axisZ * closestLocal.z;
        result.normalToShape = safe_normalize(
            frame.axisX * localNormal.x + frame.axisY * localNormal.y
                + frame.axisZ * localNormal.z,
            frame.axisX);
        result.signedDistance = distance;
        let absoluteNormal = abs(localNormal);
        var axis = 0u;
        if (absoluteNormal.y > absoluteNormal.x) { axis = 1u; }
        if (absoluteNormal.z > component_value(absoluteNormal, axis)) {
            axis = 2u;
        }
        result.feature = 0x200u + axis * 2u
            + select(0u, 1u, component_value(localNormal, axis) < 0.0);
        return result;
    }

    let gaps = frame.half - abs(local);
    var axis = 0u;
    if (gaps.y < gaps.x) { axis = 1u; }
    if (gaps.z < component_value(gaps, axis)) { axis = 2u; }
    let signValue = select(-1.0, 1.0,
        component_value(local, axis) >= 0.0);
    if (axis == 0u) { closestLocal.x = signValue * frame.half.x; }
    if (axis == 1u) { closestLocal.y = signValue * frame.half.y; }
    if (axis == 2u) { closestLocal.z = signValue * frame.half.z; }
    let outward = axis_value(frame, axis) * signValue;
    result.point = frame.center + frame.axisX * closestLocal.x
        + frame.axisY * closestLocal.y + frame.axisZ * closestLocal.z;
    result.normalToShape = -outward;
    result.signedDistance = -component_value(gaps, axis);
    result.feature = 0x200u + axis * 2u + select(0u, 1u, signValue > 0.0);
    return result;
}

fn cylinder_surface(body : u32, point : vec3<f32>,
                    frameBody : u32) -> ClosestSurface {
    let pose = compound_pose(body);
    let center = body_position_in_frame(body, frameBody);
    let local = quaternion_inverse_rotate(
        pose.orientation, point - center);
    let radius = cylinder_radius(body);
    let halfHeight = cylinder_half_height(body);
    let radialLength = length(local.xz);
    let radialDirection = select(vec2<f32>(1.0, 0.0),
        local.xz / max(radialLength, 1e-20), radialLength > 1e-10);
    let closestLocal = vec3<f32>(
        radialDirection.x * min(radialLength, radius),
        clamp(local.y, -halfHeight, halfHeight),
        radialDirection.y * min(radialLength, radius));
    let outside = radialLength > radius || abs(local.y) > halfHeight;
    var result : ClosestSurface;
    if (outside) {
        let delta = closestLocal - local;
        let distance = length(delta);
        let localNormal = safe_normalize(delta, vec3<f32>(1.0, 0.0, 0.0));
        result.point = center
            + quaternion_rotate(pose.orientation, closestLocal);
        result.normalToShape = quaternion_rotate(pose.orientation, localNormal);
        result.signedDistance = distance;
        result.feature = select(0x300u + select(0u, 1u, local.y > 0.0),
                                0x310u,
                                radialLength > radius);
        return result;
    }
    let radialGap = radius - radialLength;
    let capGap = halfHeight - abs(local.y);
    var surfaceLocal = local;
    var outward = vec3<f32>(radialDirection.x, 0.0, radialDirection.y);
    var feature = 0x310u;
    var gap = radialGap;
    if (capGap < radialGap) {
        let capSign = select(-1.0, 1.0, local.y >= 0.0);
        surfaceLocal.y = capSign * halfHeight;
        outward = vec3<f32>(0.0, capSign, 0.0);
        feature = 0x300u + select(0u, 1u, capSign > 0.0);
        gap = capGap;
    } else {
        surfaceLocal.x = radialDirection.x * radius;
        surfaceLocal.z = radialDirection.y * radius;
    }
    result.point = center
        + quaternion_rotate(pose.orientation, surfaceLocal);
    result.normalToShape = -quaternion_rotate(pose.orientation, outward);
    result.signedDistance = -gap;
    result.feature = feature;
    return result;
}

fn collide_sphere_box(sphereBody : u32, boxBody : u32,
                      frameBody : u32) -> CandidateSet {
    var result = empty_candidates();
    let sphereCenter = body_position_in_frame(sphereBody, frameBody);
    let surface = box_surface(make_box(boxBody, frameBody), sphereCenter);
    let radius = sphere_radius(sphereBody);
    result.normal = surface.normalToShape;
    append_candidate(&result,
        sphereCenter + result.normal * radius, surface.point,
        surface.signedDistance - radius, 0u, surface.feature);
    return result;
}

fn collide_sphere_cylinder(sphereBody : u32,
                           cylinderBody : u32,
                           frameBody : u32) -> CandidateSet {
    var result = empty_candidates();
    let sphereCenter = body_position_in_frame(sphereBody, frameBody);
    let surface = cylinder_surface(cylinderBody, sphereCenter, frameBody);
    let radius = sphere_radius(sphereBody);
    result.normal = surface.normalToShape;
    append_candidate(&result,
        sphereCenter + result.normal * radius, surface.point,
        surface.signedDistance - radius, 0u, surface.feature);
    return result;
}

fn closest_segment_box(segment : Segment, frame : BoxFrame) -> vec4<f32> {
    var bestFraction = 0.0;
    var bestDistance = 3.402823466e+38;
    for (var sample = 0u; sample <= 16u; sample += 1u) {
        let fraction = f32(sample) / 16.0;
        let point = mix(segment.first, segment.second, fraction);
        let distance = box_surface(frame, point).signedDistance;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestFraction = fraction;
        }
    }
    var low = max(bestFraction - 0.0625, 0.0);
    var high = min(bestFraction + 0.0625, 1.0);
    for (var iteration = 0u; iteration < 8u; iteration += 1u) {
        let left = mix(low, high, 0.3333333333);
        let right = mix(low, high, 0.6666666667);
        let leftDistance = box_surface(
            frame, mix(segment.first, segment.second, left)).signedDistance;
        let rightDistance = box_surface(
            frame, mix(segment.first, segment.second, right)).signedDistance;
        if (leftDistance <= rightDistance) { high = right; }
        else { low = left; }
    }
    bestFraction = 0.5 * (low + high);
    return vec4<f32>(mix(segment.first, segment.second, bestFraction),
                     bestFraction);
}

fn closest_segment_cylinder(segment : Segment, body : u32,
                            frameBody : u32) -> vec4<f32> {
    var bestFraction = 0.0;
    var bestDistance = 3.402823466e+38;
    for (var sample = 0u; sample <= 16u; sample += 1u) {
        let fraction = f32(sample) / 16.0;
        let point = mix(segment.first, segment.second, fraction);
        let distance = cylinder_surface(body, point, frameBody).signedDistance;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestFraction = fraction;
        }
    }
    var low = max(bestFraction - 0.0625, 0.0);
    var high = min(bestFraction + 0.0625, 1.0);
    for (var iteration = 0u; iteration < 8u; iteration += 1u) {
        let left = mix(low, high, 0.3333333333);
        let right = mix(low, high, 0.6666666667);
        let leftDistance = cylinder_surface(
            body, mix(segment.first, segment.second, left),
            frameBody).signedDistance;
        let rightDistance = cylinder_surface(
            body, mix(segment.first, segment.second, right),
            frameBody).signedDistance;
        if (leftDistance <= rightDistance) { high = right; }
        else { low = left; }
    }
    bestFraction = 0.5 * (low + high);
    return vec4<f32>(mix(segment.first, segment.second, bestFraction),
                     bestFraction);
}

fn collide_capsule_box(capsuleBody : u32, boxBody : u32,
                       frameBody : u32) -> CandidateSet {
    var result = empty_candidates();
    let segment = capsule_segment(capsuleBody, frameBody);
    let closest = closest_segment_box(segment, make_box(boxBody, frameBody));
    let surface = box_surface(make_box(boxBody, frameBody), closest.xyz);
    let radius = capsule_radius(capsuleBody);
    result.normal = surface.normalToShape;
    append_candidate(&result,
        closest.xyz + result.normal * radius, surface.point,
        surface.signedDistance - radius,
        feature_from_fraction(closest.w, 0x100u), surface.feature);
    return result;
}

fn collide_capsule_cylinder(capsuleBody : u32,
                            cylinderBody : u32,
                            frameBody : u32) -> CandidateSet {
    var result = empty_candidates();
    let segment = capsule_segment(capsuleBody, frameBody);
    let closest = closest_segment_cylinder(
        segment, cylinderBody, frameBody);
    let surface = cylinder_surface(cylinderBody, closest.xyz, frameBody);
    let radius = capsule_radius(capsuleBody);
    result.normal = surface.normalToShape;
    append_candidate(&result,
        closest.xyz + result.normal * radius, surface.point,
        surface.signedDistance - radius,
        feature_from_fraction(closest.w, 0x100u), surface.feature);
    return result;
}

fn box_projection_radius(frame : BoxFrame, axis : vec3<f32>) -> f32 {
    return frame.half.x * abs(dot(frame.axisX, axis))
         + frame.half.y * abs(dot(frame.axisY, axis))
         + frame.half.z * abs(dot(frame.axisZ, axis));
}

fn consider_box_sat_axis(sat : ptr<function, SatResult>, rawAxis : vec3<f32>,
                         kind : u32, axisA : u32, axisB : u32,
                         frameA : BoxFrame, frameB : BoxFrame) {
    let squared = dot(rawAxis, rawAxis);
    if (squared <= 1e-10) { return; }
    var axis = rawAxis * inverseSqrt(squared);
    let centerDelta = frameB.center - frameA.center;
    let signedDistance = dot(centerDelta, axis);
    if (signedDistance < 0.0) { axis = -axis; }
    let separation = abs(signedDistance)
        - box_projection_radius(frameA, axis)
        - box_projection_radius(frameB, axis);
    if (separation > currentSpeculativeDistance) {
        (*sat).valid = 0u;
    }
    let candidateCode = kind * 16u + axisA * 4u + axisB;
    let currentCode = (*sat).axisKind * 16u
        + (*sat).axisA * 4u + (*sat).axisB;
    if (separation > (*sat).separation + 1e-6
        || (abs(separation - (*sat).separation) <= 1e-6
            && candidateCode < currentCode)) {
        (*sat).normal = axis;
        (*sat).separation = separation;
        (*sat).axisKind = kind;
        (*sat).axisA = axisA;
        (*sat).axisB = axisB;
    }
}

fn box_box_sat(frameA : BoxFrame, frameB : BoxFrame) -> SatResult {
    var result : SatResult;
    result.normal = vec3<f32>(1.0, 0.0, 0.0);
    result.separation = -3.402823466e+38;
    result.axisKind = 3u;
    result.axisA = 3u;
    result.axisB = 3u;
    result.valid = 1u;
    for (var axis = 0u; axis < 3u; axis += 1u) {
        consider_box_sat_axis(&result, axis_value(frameA, axis),
                              0u, axis, 0u, frameA, frameB);
        if (result.valid == 0u) { return result; }
        consider_box_sat_axis(&result, axis_value(frameB, axis),
                              1u, 0u, axis, frameA, frameB);
        if (result.valid == 0u) { return result; }
    }
    for (var axisA = 0u; axisA < 3u; axisA += 1u) {
        for (var axisB = 0u; axisB < 3u; axisB += 1u) {
            consider_box_sat_axis(&result,
                cross(axis_value(frameA, axisA), axis_value(frameB, axisB)),
                2u, axisA, axisB, frameA, frameB);
            if (result.valid == 0u) { return result; }
        }
    }
    return result;
}

fn axis_aligned_box_sat(frameA : BoxFrame,
                        frameB : BoxFrame) -> SatResult {
    var result : SatResult;
    result.normal = vec3<f32>(1.0, 0.0, 0.0);
    result.separation = -3.402823466e+38;
    result.axisKind = 0u;
    result.axisA = 3u;
    result.axisB = 0u;
    result.valid = 1u;
    let centerDelta = frameB.center - frameA.center;
    let separations = abs(centerDelta) - frameA.half - frameB.half;
    for (var axis = 0u; axis < 3u; axis += 1u) {
        let separation = separations[axis];
        if (separation > currentSpeculativeDistance) {
            result.valid = 0u;
            return result;
        }
        if (separation > result.separation + 1e-6
            || (abs(separation - result.separation) <= 1e-6
                && axis < result.axisA)) {
            var normal = vec3<f32>(0.0);
            normal[axis] = select(-1.0, 1.0, centerDelta[axis] >= 0.0);
            result.normal = normal;
            result.separation = separation;
            result.axisA = axis;
        }
    }
    return result;
}

fn clip_against_plane(source : ClipPolygon, planeNormal : vec3<f32>,
                      planeCenter : vec3<f32>, limit : f32,
                      planeIndex : u32) -> ClipPolygon {
    var result : ClipPolygon;
    result.count = 0u;
    var localSource = source;
    if (localSource.count == 0u) { return result; }
    var previous = localSource.points[localSource.count - 1u];
    var previousDistance = dot(previous.position - planeCenter, planeNormal)
                         - limit;
    var previousInside = previousDistance <= narrow.tolerances.x;
    for (var index = 0u; index < localSource.count; index += 1u) {
        let current = localSource.points[index];
        let currentDistance = dot(current.position - planeCenter, planeNormal)
                            - limit;
        let currentInside = currentDistance <= narrow.tolerances.x;
        if (currentInside != previousInside && result.count < 8u) {
            let denominator = previousDistance - currentDistance;
            let fraction = clamp(previousDistance / denominator, 0.0, 1.0);
            result.points[result.count].position = mix(
                previous.position, current.position, fraction);
            result.points[result.count].feature = 0x280u
                + planeIndex * 32u
                + (min(previous.feature, current.feature) & 31u);
            result.count += 1u;
        }
        if (currentInside && result.count < 8u) {
            result.points[result.count] = current;
            result.count += 1u;
        }
        previous = current;
        previousDistance = currentDistance;
        previousInside = currentInside;
    }
    return result;
}

fn other_axis_first(axis : u32) -> u32 {
    if (axis == 0u) { return 1u; }
    return 0u;
}

fn other_axis_second(axis : u32) -> u32 {
    if (axis == 2u) { return 1u; }
    return 2u;
}

fn append_clipped_box_face(candidateSet : ptr<function, CandidateSet>,
                           reference : BoxFrame, incident : BoxFrame,
                           referenceNormal : vec3<f32>, referenceAxis : u32,
                           referenceIsA : bool) {
    let referenceExtent = component_value(reference.half, referenceAxis);
    let referenceCenter = reference.center
        + referenceNormal * referenceExtent;
    let sideAxisUIndex = other_axis_first(referenceAxis);
    let sideAxisVIndex = other_axis_second(referenceAxis);
    let sideAxisU = axis_value(reference, sideAxisUIndex);
    let sideAxisV = axis_value(reference, sideAxisVIndex);
    let sideExtentU = component_value(reference.half, sideAxisUIndex);
    let sideExtentV = component_value(reference.half, sideAxisVIndex);

    var incidentAxis = 0u;
    var incidentAlignment = abs(dot(referenceNormal, incident.axisX));
    for (var axis = 1u; axis < 3u; axis += 1u) {
        let alignment = abs(dot(referenceNormal, axis_value(incident, axis)));
        if (alignment > incidentAlignment + 1e-7) {
            incidentAxis = axis;
            incidentAlignment = alignment;
        }
    }
    let incidentAxisVector = axis_value(incident, incidentAxis);
    let incidentSign = select(1.0, -1.0,
        dot(referenceNormal, incidentAxisVector) > 0.0);
    let incidentCenter = incident.center + incidentAxisVector * incidentSign
        * component_value(incident.half, incidentAxis);
    let incidentUIndex = other_axis_first(incidentAxis);
    let incidentVIndex = other_axis_second(incidentAxis);
    let incidentU = axis_value(incident, incidentUIndex)
        * component_value(incident.half, incidentUIndex);
    let incidentV = axis_value(incident, incidentVIndex)
        * component_value(incident.half, incidentVIndex);
    var polygon : ClipPolygon;
    polygon.count = 4u;
    for (var corner = 0u; corner < 4u; corner += 1u) {
        // Walk the perimeter; binary corner order makes a self-crossing quad.
        let windingCorner = corner ^ (corner >> 1u);
        let signU = select(-1.0, 1.0, (windingCorner & 1u) != 0u);
        let signV = select(-1.0, 1.0, (windingCorner & 2u) != 0u);
        polygon.points[corner].position = incidentCenter
            + signU * incidentU + signV * incidentV;
        polygon.points[corner].feature = 0x240u + incidentAxis * 8u
            + select(0u, 4u, incidentSign > 0.0) + windingCorner;
    }
    polygon = clip_against_plane(
        polygon, sideAxisU, referenceCenter, sideExtentU, 0u);
    polygon = clip_against_plane(
        polygon, -sideAxisU, referenceCenter, sideExtentU, 1u);
    polygon = clip_against_plane(
        polygon, sideAxisV, referenceCenter, sideExtentV, 2u);
    polygon = clip_against_plane(
        polygon, -sideAxisV, referenceCenter, sideExtentV, 3u);

    let referenceSign = select(0u, 1u,
        dot(referenceNormal, axis_value(reference, referenceAxis)) > 0.0);
    let referenceFeature = 0x200u + referenceAxis * 2u + referenceSign;
    for (var index = 0u; index < polygon.count; index += 1u) {
        let incidentPoint = polygon.points[index].position;
        let separation = dot(incidentPoint - referenceCenter, referenceNormal);
        let referencePoint = incidentPoint - referenceNormal * separation;
        if (referenceIsA) {
            append_candidate(candidateSet, referencePoint, incidentPoint,
                             separation, referenceFeature,
                             polygon.points[index].feature);
        } else {
            append_candidate(candidateSet, incidentPoint, referencePoint,
                             separation, polygon.points[index].feature,
                             referenceFeature);
        }
    }
}

fn box_support_edge(frame : BoxFrame, direction : vec3<f32>,
                    edgeAxis : u32) -> Segment {
    var center = frame.center;
    for (var axis = 0u; axis < 3u; axis += 1u) {
        if (axis == edgeAxis) { continue; }
        let axisVector = axis_value(frame, axis);
        let signValue = select(-1.0, 1.0, dot(direction, axisVector) >= 0.0);
        center += axisVector * component_value(frame.half, axis) * signValue;
    }
    let edge = axis_value(frame, edgeAxis)
        * component_value(frame.half, edgeAxis);
    return Segment(center - edge, center + edge);
}

fn collide_box_box(bodyA : u32, bodyB : u32,
                   frameBody : u32) -> CandidateSet {
    var result = empty_candidates();
    let axisAligned = has_identity_rotation(bodyA)
        && has_identity_rotation(bodyB);
    var frameA : BoxFrame;
    var frameB : BoxFrame;
    var sat : SatResult;
    if (axisAligned) {
        frameA = make_axis_aligned_box(bodyA, frameBody);
        frameB = make_axis_aligned_box(bodyB, frameBody);
        sat = axis_aligned_box_sat(frameA, frameB);
    } else {
        frameA = make_box(bodyA, frameBody);
        frameB = make_box(bodyB, frameBody);
        sat = box_box_sat(frameA, frameB);
    }
    result.normal = sat.normal;
    if (sat.valid == 0u) { return result; }
    if (sat.axisKind == 0u) {
        append_clipped_box_face(&result, frameA, frameB,
                                sat.normal, sat.axisA, true);
    } else if (sat.axisKind == 1u) {
        append_clipped_box_face(&result, frameB, frameA,
                                -sat.normal, sat.axisB, false);
    } else {
        let edgeA = box_support_edge(frameA, sat.normal, sat.axisA);
        let edgeB = box_support_edge(frameB, -sat.normal, sat.axisB);
        let closest = closest_segments(edgeA, edgeB);
        let separation = dot(closest.pointB - closest.pointA, sat.normal);
        append_candidate(&result, closest.pointA, closest.pointB, separation,
                         0x2c0u + sat.axisA * 4u + sat.axisB,
                         0x2c0u + sat.axisB * 4u + sat.axisA);
    }
    if (result.count == 0u) {
        let radiusA = box_projection_radius(frameA, sat.normal);
        let radiusB = box_projection_radius(frameB, sat.normal);
        let pointA = frameA.center + sat.normal * radiusA;
        let pointB = frameB.center - sat.normal * radiusB;
        append_candidate(&result, pointA, pointB, sat.separation,
                         0x2f0u + sat.axisA, 0x2f0u + sat.axisB);
    }
    return result;
}

fn box_vertex(frame : BoxFrame, index : u32) -> vec3<f32> {
    let signs = vec3<f32>(
        select(-1.0, 1.0, (index & 1u) != 0u),
        select(-1.0, 1.0, (index & 2u) != 0u),
        select(-1.0, 1.0, (index & 4u) != 0u));
    return frame.center + frame.axisX * frame.half.x * signs.x
        + frame.axisY * frame.half.y * signs.y
        + frame.axisZ * frame.half.z * signs.z;
}

fn prepared_cylinder_vertex(frame : PolyFrame, index : u32,
                            radius : f32, halfHeight : f32) -> vec3<f32> {
    let ring = index & 7u;
    let angle = 0.7853981633974483 * f32(ring);
    let local = vec3<f32>(cos(angle) * radius,
        select(-halfHeight, halfHeight, index >= 8u),
        sin(angle) * radius);
    return frame.center + quaternion_rotate(frame.orientation, local);
}

fn cylinder_vertex(frame : PolyFrame, index : u32) -> vec3<f32> {
    let dimensions = max(abs(frame.dimensions), vec3<f32>(1e-5));
    let radius = 0.25 * (dimensions.x + dimensions.z);
    let halfHeight = 0.5 * max(abs(frame.dimensions.y), 1e-5);
    return prepared_cylinder_vertex(
        frame, index, radius, halfHeight);
}

fn poly_vertex_count(shapeCategory : u32) -> u32 {
    return select(8u, 16u, shapeCategory == 3u);
}

fn poly_vertex(frame : PolyFrame, shapeCategory : u32,
               index : u32) -> vec3<f32> {
    if (shapeCategory == 3u) {
        return cylinder_vertex(frame, index);
    }
    return box_vertex(make_box_from_poly(frame), index);
}

fn poly_face_axis_count(shapeCategory : u32) -> u32 {
    return select(3u, 9u, shapeCategory == 3u);
}

fn poly_face_axis(frame : PolyFrame, box : BoxFrame,
                  shapeCategory : u32, index : u32) -> vec3<f32> {
    if (shapeCategory != 3u) {
        return axis_value(box, index);
    }
    if (index == 0u) {
        return quaternion_rotate(
            frame.orientation, vec3<f32>(0.0, 1.0, 0.0));
    }
    let angle = 0.7853981633974483 * (f32(index - 1u) + 0.5);
    return quaternion_rotate(frame.orientation,
        vec3<f32>(cos(angle), 0.0, sin(angle)));
}

fn poly_edge_axis_count(shapeCategory : u32) -> u32 {
    return select(3u, 9u, shapeCategory == 3u);
}

fn poly_edge_axis(frame : PolyFrame, box : BoxFrame,
                  shapeCategory : u32, index : u32) -> vec3<f32> {
    if (shapeCategory != 3u) {
        return axis_value(box, index);
    }
    if (index == 0u) {
        return quaternion_rotate(
            frame.orientation, vec3<f32>(0.0, 1.0, 0.0));
    }
    let angle = 0.7853981633974483 * (f32(index - 1u) + 0.5);
    return quaternion_rotate(frame.orientation,
        vec3<f32>(-sin(angle), 0.0, cos(angle)));
}

fn projected_poly_maximum(frame : PolyFrame, box : BoxFrame,
                          shapeCategory : u32, axis : vec3<f32>) -> f32 {
    if (shapeCategory != 3u) {
        var maximum = dot(box_vertex(box, 0u), axis);
        for (var index = 1u; index < 8u; index += 1u) {
            maximum = max(maximum, dot(box_vertex(box, index), axis));
        }
        return maximum;
    }
    let dimensions = max(abs(frame.dimensions), vec3<f32>(1e-5));
    let radius = 0.25 * (dimensions.x + dimensions.z);
    let halfHeight = 0.5 * max(abs(frame.dimensions.y), 1e-5);
    var maximum = dot(
        prepared_cylinder_vertex(frame, 0u, radius, halfHeight), axis);
    for (var index = 1u; index < 16u; index += 1u) {
        let projection = dot(
            prepared_cylinder_vertex(
                frame, index, radius, halfHeight),
            axis);
        maximum = max(maximum, projection);
    }
    return maximum;
}

fn projected_poly_minimum(frame : PolyFrame, box : BoxFrame,
                          shapeCategory : u32, axis : vec3<f32>) -> f32 {
    if (shapeCategory != 3u) {
        var minimum = dot(box_vertex(box, 0u), axis);
        for (var index = 1u; index < 8u; index += 1u) {
            minimum = min(minimum, dot(box_vertex(box, index), axis));
        }
        return minimum;
    }
    let dimensions = max(abs(frame.dimensions), vec3<f32>(1e-5));
    let radius = 0.25 * (dimensions.x + dimensions.z);
    let halfHeight = 0.5 * max(abs(frame.dimensions.y), 1e-5);
    var minimum = dot(
        prepared_cylinder_vertex(frame, 0u, radius, halfHeight), axis);
    for (var index = 1u; index < 16u; index += 1u) {
        let projection = dot(
            prepared_cylinder_vertex(
                frame, index, radius, halfHeight),
            axis);
        minimum = min(minimum, projection);
    }
    return minimum;
}

fn consider_poly_sat_axis(sat : ptr<function, SatResult>,
                          rawAxis : vec3<f32>, kind : u32,
                          axisA : u32, axisB : u32,
                          categoryA : u32, categoryB : u32,
                          frameA : PolyFrame, frameB : PolyFrame,
                          boxA : BoxFrame, boxB : BoxFrame,
                          centerDelta : vec3<f32>) {
    let squared = dot(rawAxis, rawAxis);
    if (squared <= 1e-10) { return; }
    var axis = rawAxis * inverseSqrt(squared);
    if (dot(centerDelta, axis) < 0.0) {
        axis = -axis;
    }
    let maximumA = projected_poly_maximum(
        frameA, boxA, categoryA, axis);
    let minimumB = projected_poly_minimum(
        frameB, boxB, categoryB, axis);
    let separation = minimumB - maximumA;
    if (separation > currentSpeculativeDistance) { (*sat).valid = 0u; }
    let candidateCode = kind * 256u + axisA * 16u + axisB;
    let currentCode = (*sat).axisKind * 256u
        + (*sat).axisA * 16u + (*sat).axisB;
    if (separation > (*sat).separation + 1e-6
        || (abs(separation - (*sat).separation) <= 1e-6
            && candidateCode < currentCode)) {
        (*sat).normal = axis;
        (*sat).separation = separation;
        (*sat).axisKind = kind;
        (*sat).axisA = axisA;
        (*sat).axisB = axisB;
    }
}

fn polyhedron_sat(categoryA : u32, categoryB : u32,
                  frameA : PolyFrame, frameB : PolyFrame,
                  boxA : BoxFrame, boxB : BoxFrame,
                  centerDelta : vec3<f32>) -> SatResult {
    var result : SatResult;
    result.normal = vec3<f32>(1.0, 0.0, 0.0);
    result.separation = -3.402823466e+38;
    result.axisKind = 3u;
    result.axisA = 15u;
    result.axisB = 15u;
    result.valid = 1u;
    for (var axis = 0u; axis < poly_face_axis_count(categoryA);
        axis += 1u) {
        consider_poly_sat_axis(&result,
            poly_face_axis(frameA, boxA, categoryA, axis),
            0u, axis, 0u, categoryA, categoryB,
            frameA, frameB, boxA, boxB, centerDelta);
        if (result.valid == 0u) { return result; }
    }
    for (var axis = 0u; axis < poly_face_axis_count(categoryB);
        axis += 1u) {
        consider_poly_sat_axis(&result,
            poly_face_axis(frameB, boxB, categoryB, axis),
            1u, 0u, axis, categoryA, categoryB,
            frameA, frameB, boxA, boxB, centerDelta);
        if (result.valid == 0u) { return result; }
    }
    for (var axisA = 0u; axisA < poly_edge_axis_count(categoryA);
         axisA += 1u) {
        let edgeAxisA = poly_edge_axis(
            frameA, boxA, categoryA, axisA);
        for (var axisB = 0u; axisB < poly_edge_axis_count(categoryB);
             axisB += 1u) {
            consider_poly_sat_axis(&result, cross(
                edgeAxisA,
                poly_edge_axis(frameB, boxB, categoryB, axisB)),
                2u, axisA, axisB,
                categoryA, categoryB, frameA, frameB,
                boxA, boxB, centerDelta);
            if (result.valid == 0u) { return result; }
        }
    }
    return result;
}

fn collide_polyhedra(bodyA : u32, categoryA : u32,
                     bodyB : u32, categoryB : u32,
                     frameBody : u32) -> CandidateSet {
    var result = empty_candidates();
    let centerA = body_position_in_frame(bodyA, frameBody);
    let centerB = body_position_in_frame(bodyB, frameBody);
    let centerDelta = centerB - centerA;
    let frameA = make_poly_frame(bodyA, centerA);
    let frameB = make_poly_frame(bodyB, centerB);
    var boxA : BoxFrame;
    var boxB : BoxFrame;
    if (categoryA != 3u) { boxA = make_box_from_poly(frameA); }
    if (categoryB != 3u) { boxB = make_box_from_poly(frameB); }
    let sat = polyhedron_sat(
        categoryA, categoryB, frameA, frameB,
        boxA, boxB, centerDelta);
    result.normal = sat.normal;
    if (sat.valid == 0u) { return result; }
    let maximumA = projected_poly_maximum(
        frameA, boxA, categoryA, sat.normal);
    let minimumB = projected_poly_minimum(
        frameB, boxB, categoryB, sat.normal);
    let separation = minimumB - maximumA;
    let supportTolerance = max(4.0 * narrow.tolerances.x, 1e-4);
    let countA = poly_vertex_count(categoryA);
    let countB = poly_vertex_count(categoryB);
    var supportA = 0u;
    var supportB = 0u;
    var averageA = vec3<f32>(0.0);
    var averageB = vec3<f32>(0.0);
    var firstA = 0u;
    var firstB = 0u;
    for (var index = 0u; index < countA; index += 1u) {
        let vertex = poly_vertex(frameA, categoryA, index);
        if (dot(vertex, sat.normal) >= maximumA - supportTolerance) {
            if (supportA == 0u) { firstA = index; }
            averageA += vertex;
            supportA += 1u;
        }
    }
    for (var index = 0u; index < countB; index += 1u) {
        let vertex = poly_vertex(frameB, categoryB, index);
        if (dot(vertex, sat.normal) <= minimumB + supportTolerance) {
            if (supportB == 0u) { firstB = index; }
            averageB += vertex;
            supportB += 1u;
        }
    }
    averageA /= f32(max(supportA, 1u));
    averageB /= f32(max(supportB, 1u));
    let transverseMidpoint = 0.5 * (averageA + averageB);
    let transverse = transverseMidpoint
        - sat.normal * dot(transverseMidpoint, sat.normal);
    let primaryA = transverse + sat.normal * maximumA;
    let primaryB = transverse + sat.normal * minimumB;
    append_candidate(&result, primaryA, primaryB, separation,
                     0x400u + firstA, 0x400u + firstB);
    for (var index = 0u; index < countA && result.count < MAX_CANDIDATES;
         index += 1u) {
        let vertex = poly_vertex(frameA, categoryA, index);
        if (dot(vertex, sat.normal) >= maximumA - supportTolerance) {
            append_candidate(&result, vertex,
                vertex + sat.normal * separation, separation,
                0x400u + index, 0x480u + firstB);
        }
    }
    for (var index = 0u; index < countB && result.count < MAX_CANDIDATES;
         index += 1u) {
        let vertex = poly_vertex(frameB, categoryB, index);
        if (dot(vertex, sat.normal) <= minimumB + supportTolerance) {
            append_candidate(&result,
                vertex - sat.normal * separation, vertex, separation,
                0x480u + firstA, 0x400u + index);
        }
    }
    return result;
}

fn pair_key_less(lowA : u32, highA : u32,
                 lowB : u32, highB : u32) -> bool {
    return highA < highB || (highA == highB && lowA < lowB);
}

fn find_previous_manifold(bodyA : u32, bodyB : u32) -> u32 {
    var low = 0u;
    var high = min(atomicLoad(&narrowTelemetry[22]), narrow.capacities.z);
    while (low < high) {
        let middle = low + (high - low) / 2u;
        let pair = previousManifolds[middle].pair;
        if (pair_key_less(pair.keyLow, pair.keyHigh, bodyB, bodyA)) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    if (low < min(atomicLoad(&narrowTelemetry[22]), narrow.capacities.z)) {
        let pair = previousManifolds[low].pair;
        if (pair.keyHigh == bodyA && pair.keyLow == bodyB) { return low; }
    }
    return SENTINEL;
}

fn candidate_precedes(a : ContactCandidate, b : ContactCandidate) -> bool {
    if (a.pointA_separation.w != b.pointA_separation.w) {
        return a.pointA_separation.w < b.pointA_separation.w;
    }
    if (a.features.x != b.features.x) { return a.features.x < b.features.x; }
    return a.features.y < b.features.y;
}

fn reduce_candidates(sourceInput : CandidateSet) -> ReducedCandidateSet {
    var source = sourceInput;
    var result = empty_reduced_candidates();
    result.normal = source.normal;
    if (source.count == 0u) { return result; }
    for (var index = 1u; index < source.count; index += 1u) {
        let item = source.items[index];
        var insertion = index;
        // Keep sentinel/underflow indices outside expressions. Hardware WGSL
        // lowering can evaluate a guarded value before short-circuit selection.
        while (insertion > 0u) {
            if (!candidate_precedes(item, source.items[insertion - 1u])) { break; }
            source.items[insertion] = source.items[insertion - 1u];
            insertion -= 1u;
        }
        source.items[insertion] = item;
    }
    var selected = array<u32, 4>(SENTINEL, SENTINEL, SENTINEL, SENTINEL);
    selected[0] = 0u;
    result.items[0] = source.items[0];
    result.count = 1u;
    let wanted = min(source.count, 4u);
    for (var slot = 1u; slot < wanted; slot += 1u) {
        var best = SENTINEL;
        var bestSpread = -1.0;
        for (var candidate = 1u; candidate < source.count;
             candidate += 1u) {
            var alreadySelected = false;
            for (var prior = 0u; prior < slot; prior += 1u) {
                alreadySelected = alreadySelected
                    || selected[prior] == candidate;
            }
            if (alreadySelected) { continue; }
            let midpoint = 0.5 * (
                source.items[candidate].pointA_separation.xyz
                + source.items[candidate].pointB.xyz);
            var minimumSpread = 3.402823466e+38;
            for (var prior = 0u; prior < slot; prior += 1u) {
                let priorMidpoint = 0.5 * (
                    source.items[selected[prior]].pointA_separation.xyz
                    + source.items[selected[prior]].pointB.xyz);
                let delta = midpoint - priorMidpoint;
                let tangentDelta = delta - result.normal
                    * dot(delta, result.normal);
                minimumSpread = min(minimumSpread,
                                    dot(tangentDelta, tangentDelta));
            }
            var replace = best == SENTINEL || minimumSpread > bestSpread + 1e-9;
            if (best != SENTINEL && abs(minimumSpread - bestSpread) <= 1e-9) {
                replace = candidate_precedes(source.items[candidate], source.items[best]);
            }
            if (replace) {
                best = candidate;
                bestSpread = minimumSpread;
            }
        }
        if (best == SENTINEL) { break; }
        selected[slot] = best;
        result.items[slot] = source.items[best];
        result.count += 1u;
    }
    return result;
}

fn tangent_basis(normal : vec3<f32>) -> vec3<f32> {
    var helper = vec3<f32>(1.0, 0.0, 0.0);
    if (abs(normal.x) > 0.577350269) {
        helper = vec3<f32>(0.0, 1.0, 0.0);
    }
    return safe_normalize(cross(helper, normal),
                          vec3<f32>(0.0, 0.0, 1.0));
}

fn base_manifold(pairRecord : KeyValue,
                 candidateCount : u32) -> ContactManifold {
    let bodyA = pairRecord.keyHigh;
    let bodyB = pairRecord.keyLow;
    let pairClass = pairRecord.value & 255u;
    let sleepingFlag = (pairRecord.value >> 8u) & 1u;
    var result = empty_manifold();
    result.pair = KeyValue(bodyB, bodyA, sleepingFlag, pairRecord.ordinal);
    result.state = vec4<u32>(candidateCount, pairClass, sleepingFlag, 0u);
    return result;
}

fn build_manifold(pairRecord : KeyValue,
                  sourceCandidates : CandidateSet) -> ContactManifold {
    let bodyA = pairRecord.keyHigh;
    let bodyB = pairRecord.keyLow;
    let sleepingFlag = (pairRecord.value >> 8u) & 1u;
    var candidates = reduce_candidates(sourceCandidates);
    var result = base_manifold(pairRecord, candidates.count);
    if (candidates.count == 0u) { return result; }

    let normal = safe_normalize(candidates.normal,
                                vec3<f32>(1.0, 0.0, 0.0));
    let tangent1 = tangent_basis(normal);
    let tangent2 = cross(normal, tangent1);
    var maximumSeparation = -3.402823466e+38;
    var weightedCenter = vec3<f32>(0.0);
    var totalWeight = 0.0;
    let previousIndex = find_previous_manifold(bodyA, bodyB);
    var previous = empty_manifold();
    var normalCoherent = false;
    if (previousIndex != SENTINEL) {
        previous = previousManifolds[previousIndex];
        normalCoherent = dot(normal, previous.normal.xyz) > 0.5;
        result.state.w = previous.state.w + 1u;
        result.state.z = sleepingFlag | (previous.state.z & 0xff00u);
    }
    var usedPrevious = array<u32, 4>(0u, 0u, 0u, 0u);

    for (var pointIndex = 0u; pointIndex < candidates.count;
         pointIndex += 1u) {
        let candidate = candidates.items[pointIndex];
        let localA = quaternion_inverse_rotate(compound_pose(bodyA).orientation,
            candidate.pointA_separation.xyz
                - body_position_in_frame(bodyA, bodyA));
        let localB = quaternion_inverse_rotate(compound_pose(bodyB).orientation,
            candidate.pointB.xyz - body_position_in_frame(bodyB, bodyA));
        result.points[pointIndex].localAnchorA_separation = vec4<f32>(
            localA, candidate.pointA_separation.w);
        result.points[pointIndex].localAnchorB_normalImpulse = vec4<f32>(
            localB, 0.0);
        result.points[pointIndex].features = vec4<u32>(
            candidate.features.xy, 0u, 0u);
        result.points[pointIndex].impulses = vec4<f32>(0.0);
        maximumSeparation = max(maximumSeparation,
                                candidate.pointA_separation.w);
        let weight = clamp(1.0
            - max(candidate.pointA_separation.w, 0.0)
              / max(currentSpeculativeDistance, 1e-6), 0.05, 1.0);
        weightedCenter += 0.5 * (candidate.pointA_separation.xyz
                               + candidate.pointB.xyz) * weight;
        totalWeight += weight;

        var matchedPoint = SENTINEL;
        if (previousIndex != SENTINEL) {
            for (var oldPoint = 0u; oldPoint < previous.state.x;
                 oldPoint += 1u) {
                if (usedPrevious[oldPoint] == 0u
                    && previous.points[oldPoint].features.x
                       == candidate.features.x
                    && previous.points[oldPoint].features.y
                       == candidate.features.y) {
                    matchedPoint = oldPoint;
                    break;
                }
            }
        }
        var recycled = false;
        if (matchedPoint == SENTINEL && previousIndex != SENTINEL) {
            let recycleSquared = narrow.tolerances.z * narrow.tolerances.z;
            var bestDistance = recycleSquared;
            for (var oldPoint = 0u; oldPoint < previous.state.x;
                 oldPoint += 1u) {
                if (usedPrevious[oldPoint] != 0u) { continue; }
                let deltaA = localA
                    - previous.points[oldPoint].localAnchorA_separation.xyz;
                let deltaB = localB
                    - previous.points[oldPoint].localAnchorB_normalImpulse.xyz;
                let distance = dot(deltaA, deltaA) + dot(deltaB, deltaB);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    matchedPoint = oldPoint;
                }
            }
            recycled = matchedPoint != SENTINEL;
        }
        if (matchedPoint != SENTINEL) {
            usedPrevious[matchedPoint] = 1u;
            result.points[pointIndex].features.z =
                previous.points[matchedPoint].features.z + 1u;
            if (normalCoherent) {
                result.points[pointIndex].localAnchorB_normalImpulse.w =
                    previous.points[matchedPoint].localAnchorB_normalImpulse.w;
                result.points[pointIndex].impulses =
                    previous.points[matchedPoint].impulses;
            }
            if (recycled) {
                atomicAdd(&narrowTelemetry[15], 1u);
            } else {
                atomicAdd(&narrowTelemetry[14], 1u);
            }
        }
    }

    let center = weightedCenter / max(totalWeight, 1e-7);
    result.normal = vec4<f32>(normal, maximumSeparation);
    result.tangent1 = vec4<f32>(tangent1, 0.0);
    result.tangent2 = vec4<f32>(tangent2, 0.0);
    result.frictionAnchorA = vec4<f32>(quaternion_inverse_rotate(
        compound_pose(bodyA).orientation,
        center - body_position_in_frame(bodyA, bodyA)), 0.0);
    result.frictionAnchorB = vec4<f32>(quaternion_inverse_rotate(
        compound_pose(bodyB).orientation,
        center - body_position_in_frame(bodyB, bodyA)), 0.0);
    if (previousIndex != SENTINEL && normalCoherent) {
        let oldFriction = previous.tangent1.xyz * previous.tangent1.w
                        + previous.tangent2.xyz * previous.tangent2.w;
        result.tangent1.w = dot(oldFriction, tangent1);
        result.tangent2.w = dot(oldFriction, tangent2);
        result.frictionAnchorA.w = previous.frictionAnchorA.w;
        result.rollingImpulse = previous.rollingImpulse;
    }

    let manifoldRank = atomicAdd(&narrowTelemetry[11], 1u) + 1u;
    let pointRank = atomicAdd(&narrowTelemetry[12], candidates.count)
                  + candidates.count;
    atomicMax(&narrowTelemetry[19], manifoldRank);
    atomicMax(&narrowTelemetry[20], pointRank);
    var speculative = true;
    for (var pointIndex = 0u; pointIndex < candidates.count;
         pointIndex += 1u) {
        speculative = speculative
            && candidates.items[pointIndex].pointA_separation.w > 0.0;
    }
    if (speculative) { atomicAdd(&narrowTelemetry[13], 1u); }
    return result;
}

fn class_pair_record(gid : vec3<u32>, pairClass : u32) -> KeyValue {
    let localIndex = gid.x;
    if (localIndex >= collisionClasses.words[pairClass / 4u][pairClass % 4u]) {
        return KeyValue(0u, 0u, 0u, SENTINEL);
    }
    let offsetIndex = PAIR_CLASS_COUNT + pairClass;
    let bucketIndex = collisionClasses.words[offsetIndex / 4u][offsetIndex % 4u] + localIndex;
    let pairRecord = bucketedPairs[bucketIndex];
    currentSpeculativeDistance = pair_speculative_distance(
        pairRecord.keyHigh, pairRecord.keyLow);
    return pairRecord;
}

// A small stud must not inherit the distant corners of the box it touches.
// Keep only points on both child shapes, then one patch centre per stud.
// The parent reduction spreads four contacts across all supporting studs.
fn collide_lego_polyhedron_parts(a:u32,ca:u32,b:u32,cb:u32)->CandidateSet {
    var hit=collide_polyhedra(a,ca,b,cb,a);
    var result=empty_candidates();result.normal=hit.normal;
    var pointA=vec3<f32>(0);var pointB=vec3<f32>(0);var separation=0.0;var count=0u;var features=vec2<u32>(0);
    let boxA=make_box(a,a);let boxB=make_box(b,a);
    for(var k=0u;k<hit.count;k++) {
        let c=hit.items[k];
        var da=box_surface(boxA,c.pointA_separation.xyz).signedDistance;
        var db=box_surface(boxB,c.pointB.xyz).signedDistance;
        if(ca==3u){da=cylinder_surface(a,c.pointA_separation.xyz,a).signedDistance;}
        if(cb==3u){db=cylinder_surface(b,c.pointB.xyz,a).signedDistance;}
        if(da>narrow.tolerances.x*2.0||db>narrow.tolerances.x*2.0){continue;}
        pointA+=c.pointA_separation.xyz;pointB+=c.pointB.xyz;separation+=c.pointA_separation.w;
        if(count==0u){features=c.features.xy;}count++;
    }
    if(count>0u){let n=f32(count);append_candidate(&result,pointA/n,pointB/n,separation/n,features.x,features.y);}
    return result;
}

// Keep compound work in the four pair classes that can contain a box.
// Referencing the generic compound dispatcher from all ten entry points
// crashed SwiftShader on first submission. Specializing the reachable routines
// avoids that failure without changing the supported child contacts.
fn prepare_lego_parts(a : u32, b : u32, i : u32, j : u32) -> bool {
    compoundA = a;
    compoundB = b;
    compoundPoseA = poses[a];
    compoundPoseB = poses[b];
    compoundShapeA = shapes[a];
    compoundShapeB = shapes[b];
    if (legoIsBrick(bitcast<u32>(compoundShapeA.invInertia_material.w))) {
        let dim = compoundShapeA.dimensions_type.xyz;
        compoundShapeA.dimensions_type = vec4<f32>(
            legoBrickPartSize(dim, i), select(2.0, 4.0, i > 0u));
        compoundPoseA.position_invMass = vec4<f32>(
            compoundPoseA.position_invMass.xyz + quaternion_rotate(
                compoundPoseA.orientation, legoBrickPartOffset(dim, i)),
            compoundPoseA.position_invMass.w);
    }
    if (legoIsBrick(bitcast<u32>(compoundShapeB.invInertia_material.w))) {
        let dim = compoundShapeB.dimensions_type.xyz;
        compoundShapeB.dimensions_type = vec4<f32>(
            legoBrickPartSize(dim, j), select(2.0, 4.0, j > 0u));
        compoundPoseB.position_invMass = vec4<f32>(
            compoundPoseB.position_invMass.xyz + quaternion_rotate(
                compoundPoseB.orientation, legoBrickPartOffset(dim, j)),
            compoundPoseB.position_invMass.w);
    }
    compoundEnabled = true;
    let delta = body_position_in_frame(b, a) - body_position_in_frame(a, a);
    let radius = .5 * (length(compoundShapeA.dimensions_type.xyz)
                     + length(compoundShapeB.dimensions_type.xyz))
               + currentSpeculativeDistance;
    return dot(delta, delta) <= radius * radius;
}

fn append_lego_contacts(result : ptr<function, CandidateSet>,
                        deepest : ptr<function, f32>,
                        candidates : CandidateSet, i : u32, j : u32) {
    var hit = candidates;
    if (hit.count == 0u) { return; }
    var separation = 1e30;
    for (var k = 0u; k < hit.count; k++) {
        separation = min(separation, hit.items[k].pointA_separation.w);
    }
    if ((*result).count > 0u && dot((*result).normal, hit.normal) < .9) {
        if (separation >= *deepest) { return; }
        *result = empty_candidates();
    }
    (*result).normal = hit.normal;
    *deepest = min(*deepest, separation);
    for (var k = 0u; k < hit.count; k++) {
        let c = hit.items[k];
        append_candidate(result, c.pointA_separation.xyz, c.pointB.xyz,
            c.pointA_separation.w, c.features.x | (i << 12u),
            c.features.y | (j << 12u));
    }
}

fn collide_lego_sphere(a : u32, b : u32) -> CandidateSet {
    let sa = shapes[a];
    let sb = shapes[b];
    let na = legoBrickParts(sa.dimensions_type.xyz, bitcast<u32>(sa.invInertia_material.w));
    let nb = legoBrickParts(sb.dimensions_type.xyz, bitcast<u32>(sb.invInertia_material.w));
    var result = empty_candidates();
    var deepest = 1e30;
    // At most 9 x 9 child pairs, with a bounding-sphere rejection before SAT.
    for (var i = 0u; i < na; i++) {
        for (var j = 0u; j < nb; j++) {
            if (!prepare_lego_parts(a, b, i, j)) { continue; }
            let ca = canonical_shape(compoundShapeA.dimensions_type.w);
            let cb = canonical_shape(compoundShapeB.dimensions_type.w);
            var hit = empty_candidates();
            if (ca == 0u) {
                if (cb == 2u) { hit = collide_sphere_box(a, b, a); }
                else { hit = collide_sphere_cylinder(a, b, a); }
            } else {
                if (ca == 2u) { hit = swap_candidates(collide_sphere_box(b, a, a)); }
                else { hit = swap_candidates(collide_sphere_cylinder(b, a, a)); }
            }
            append_lego_contacts(&result, &deepest, hit, i, j);
        }
    }
    compoundEnabled = false;
    return result;
}

fn collide_lego_capsule(a : u32, b : u32) -> CandidateSet {
    let sa = shapes[a];
    let sb = shapes[b];
    let na = legoBrickParts(sa.dimensions_type.xyz, bitcast<u32>(sa.invInertia_material.w));
    let nb = legoBrickParts(sb.dimensions_type.xyz, bitcast<u32>(sb.invInertia_material.w));
    var result = empty_candidates();
    var deepest = 1e30;
    // At most 9 x 9 child pairs, with a bounding-sphere rejection before SAT.
    for (var i = 0u; i < na; i++) {
        for (var j = 0u; j < nb; j++) {
            if (!prepare_lego_parts(a, b, i, j)) { continue; }
            let ca = canonical_shape(compoundShapeA.dimensions_type.w);
            let cb = canonical_shape(compoundShapeB.dimensions_type.w);
            var hit = empty_candidates();
            if (ca == 1u) {
                if (cb == 2u) { hit = collide_capsule_box(a, b, a); }
                else { hit = collide_capsule_cylinder(a, b, a); }
            } else {
                if (ca == 2u) { hit = swap_candidates(collide_capsule_box(b, a, a)); }
                else { hit = swap_candidates(collide_capsule_cylinder(b, a, a)); }
            }
            append_lego_contacts(&result, &deepest, hit, i, j);
        }
    }
    compoundEnabled = false;
    return result;
}

fn collide_lego_polyhedra(a : u32, b : u32) -> CandidateSet {
    let sa = shapes[a];
    let sb = shapes[b];
    let na = legoBrickParts(sa.dimensions_type.xyz, bitcast<u32>(sa.invInertia_material.w));
    let nb = legoBrickParts(sb.dimensions_type.xyz, bitcast<u32>(sb.invInertia_material.w));
    var result = empty_candidates();
    var deepest = 1e30;
    // At most 9 x 9 child pairs, with a bounding-sphere rejection before SAT.
    for (var i = 0u; i < na; i++) {
        for (var j = 0u; j < nb; j++) {
            if (!prepare_lego_parts(a, b, i, j)) { continue; }
            let ca = canonical_shape(compoundShapeA.dimensions_type.w);
            let cb = canonical_shape(compoundShapeB.dimensions_type.w);
            var hit = empty_candidates();
            if (ca == 2u && cb == 2u) {
                hit = collide_box_box(a, b, a);
            } else {
                hit = collide_lego_polyhedron_parts(a, ca, b, cb);
            }
            append_lego_contacts(&result, &deepest, hit, i, j);
        }
    }
    compoundEnabled = false;
    return result;
}

fn body_has_authored(body: u32) -> bool {
    return any(shapes[body].authored_shape != vec4<u32>(0u));
}
fn pair_has_authored(pair: KeyValue) -> bool {
    return body_has_authored(pair.keyHigh) || body_has_authored(pair.keyLow);
}

// These conversions always use the parent COM pose. Temporary cell poses
// must never change the frame of either the heap or the final solver anchors.
fn contact_root_point(body: u32, shape: AuthoredShapeView,
                      point: vec3<f32>, frameBody: u32) -> vec3<f32> {
    let center = body_position_in_frame(body, frameBody)
        + poses[body].position_invMass.xyz - compound_pose(body).position_invMass.xyz;
    return authored_root_point(shape, quaternion_inverse_rotate(
        poses[body].orientation, point - center));
}
fn contact_world_point(body: u32, shape: AuthoredShapeView,
                       point: vec3<f32>, frameBody: u32) -> vec3<f32> {
    let center = body_position_in_frame(body, frameBody)
        + poses[body].position_invMass.xyz - compound_pose(body).position_invMass.xyz;
    return center + quaternion_rotate(poses[body].orientation,
        authored_body_point(shape, point));
}
fn contact_world_vector(body: u32, shape: AuthoredShapeView,
                        vector: vec3<f32>) -> vec3<f32> {
    return quaternion_rotate(poses[body].orientation, authored_body_vector(shape, vector));
}

fn collide_authored_round(roundBody: u32, authoredBody: u32,
                          frameBody: u32, capsule: bool) -> CandidateSet {
    var result = empty_candidates();
    let shape = authored_shape_ref(shapes[authoredBody].authored_shape);
    if (!shape.valid) { atomicAdd(&narrowTelemetry[16], 1u); return result; }
    let center = body_position_in_frame(roundBody, frameBody);
    var segment = Segment(center, center);
    var radius = sphere_radius(roundBody);
    if (capsule) { segment = capsule_segment(roundBody, frameBody); radius = capsule_radius(roundBody); }
    let a = contact_root_point(authoredBody, shape, segment.first, frameBody);
    let b = contact_root_point(authoredBody, shape, segment.second, frameBody);
    let surface = authored_segment_surface(shape, a, b);
    if (!surface.valid) { atomicAdd(&narrowTelemetry[16], 1u); return result; }
    let point = contact_world_point(authoredBody, shape, surface.point, frameBody);
    let closest = closest_point_segment(point, segment);
    result.normal = -contact_world_vector(authoredBody, shape, surface.normal);
    append_candidate(&result, closest.xyz + result.normal * radius, point,
        surface.distance - radius, select(0u, feature_from_fraction(closest.w, 0x100u), capsule),
        surface.feature);
    return result;
}

fn authored_cell_pose(body: u32, shape: AuthoredShapeView, cell: AuthoredShapeCell) -> BodyPose {
    var result = poses[body];
    result.position_invMass = vec4<f32>(contact_world_point(body, shape,
        .5 * (cell.minimum + cell.maximum), body), result.position_invMass.w);
    result.orientation = authored_root_orientation(shape, result.orientation);
    return result;
}

// Return a durable exterior-face ordinal only for an actual surface point.
// A child cell's buried mating face is never accepted as a contact feature.
fn authored_contact_feature(body: u32, shape: AuthoredShapeView, cell: AuthoredShapeCell,
                            point: vec3<f32>, outward: vec3<f32>, frameBody: u32) -> u32 {
    let rootPoint = contact_root_point(body, shape, point, frameBody);
    let rootNormal = authored_root_vector(shape,
        quaternion_inverse_rotate(poses[body].orientation, outward));
    let tolerance = max(1e-4, narrow.tolerances.x * .05);
    for (var f = cell.first_face; f < cell.first_face + cell.face_count; f++) {
        let face = authored_face(shape, f);
        if (!face.valid) { return SENTINEL; }
        if (rootNormal[face.axis] * f32(face.sign) <= 1e-5) { continue; }
        if (all(rootPoint >= face.minimum - vec3<f32>(tolerance))
            && all(rootPoint <= face.maximum + vec3<f32>(tolerance))) { return 0x80000000u | f; }
    }
    return SENTINEL;
}

// Restrict a cell's full face to one clipped exterior rectangle. Keep its
// depth/axes so the existing polygon clipper chooses the same support plane.
fn authored_face_box(frame: BoxFrame, cell: AuthoredShapeCell, face: AuthoredShapeFace) -> BoxFrame {
    var result = frame;
    let delta = .5 * (face.minimum + face.maximum - cell.minimum - cell.maximum);
    for (var axis = 0u; axis < 3u; axis++) {
        if (axis == face.axis) { continue; }
        result.center += axis_value(frame, axis) * delta[axis];
        result.half[axis] = .5 * (face.maximum[axis] - face.minimum[axis]);
    }
    return result;
}

fn authored_face_clips(reference: BoxFrame, incident: BoxFrame,
                       refShape: AuthoredShapeView, incShape: AuthoredShapeView,
                       refCell: AuthoredShapeCell, incCell: AuthoredShapeCell,
                       normal: vec3<f32>, axis: u32) -> CandidateSet {
    var result = empty_candidates(); result.normal = normal;
    var incAxis = 0u;
    for (var k = 1u; k < 3u; k++) {
        if (abs(dot(normal, axis_value(incident, k)))
            > abs(dot(normal, axis_value(incident, incAxis))) + 1e-7) { incAxis = k; }
    }
    let refCount = select(1u, refCell.face_count, refShape.valid);
    let incCount = select(1u, incCell.face_count, incShape.valid);
    for (var i = 0u; i < refCount; i++) {
        var refBox = reference;
        if (refShape.valid) {
            let face = authored_face(refShape, refCell.first_face + i);
            if (!face.valid || face.axis != axis
                || dot(normal, axis_value(reference, axis)) * f32(face.sign) <= 0.0) { continue; }
            refBox = authored_face_box(reference, refCell, face);
        }
        for (var j = 0u; j < incCount; j++) {
            var incBox = incident;
            if (incShape.valid) {
                let face = authored_face(incShape, incCell.first_face + j);
                if (!face.valid || face.axis != incAxis
                    || dot(normal, axis_value(incident, incAxis)) * f32(face.sign) >= 0.0) { continue; }
                incBox = authored_face_box(incident, incCell, face);
            }
            append_clipped_box_face(&result, refBox, incBox, normal, axis, true);
        }
    }
    return result;
}

fn collide_authored_box_cells(a: u32, b: u32, sa: AuthoredShapeView, sb: AuthoredShapeView,
                              cellA: AuthoredShapeCell, cellB: AuthoredShapeCell) -> CandidateSet {
    let boxA = make_box(a, a); let boxB = make_box(b, a);
    let sat = box_box_sat(boxA, boxB);
    var hit = empty_candidates(); hit.normal = sat.normal;
    if (sat.valid == 0u) { return hit; }
    if (sat.axisKind == 0u) {
        hit = authored_face_clips(boxA, boxB, sa, sb, cellA, cellB, sat.normal, sat.axisA);
    } else if (sat.axisKind == 1u) {
        hit = swap_candidates(authored_face_clips(boxB, boxA, sb, sa, cellB, cellA, -sat.normal, sat.axisB));
    } else {
        let closest = closest_segments(box_support_edge(boxA, sat.normal, sat.axisA),
            box_support_edge(boxB, -sat.normal, sat.axisB));
        append_candidate(&hit, closest.pointA, closest.pointB,
            dot(closest.pointB - closest.pointA, sat.normal), 0x2c0u + sat.axisA, 0x2c0u + sat.axisB);
    }
    return hit;
}

// B is always an authored shape. Visit its preorder BVH against each A child;
// clipped patches eliminate internal cell seams before parent reduction.
fn collide_authored_polyhedra_ordered(a: u32, b: u32) -> CandidateSet {
    var result = empty_candidates();
    let sa = authored_shape_ref(shapes[a].authored_shape);
    let sb = authored_shape_ref(shapes[b].authored_shape);
    if (!sb.valid || (body_has_authored(a) && !sa.valid)) {
        atomicAdd(&narrowTelemetry[16], 1u); return result;
    }
    let countA = select(legoBrickParts(shapes[a].dimensions_type.xyz,
        bitcast<u32>(shapes[a].invInertia_material.w)), sa.cell_count, sa.valid);
    var deepest = 1e30;
    for (var i = 0u; i < countA; i++) {
        var cellA: AuthoredShapeCell;
        compoundEnabled = false;
        // Prepares a normal primitive or a LEGO child without altering its parent.
        let unused = prepare_lego_parts(a, b, i, 0u);
        if (sa.valid) {
            cellA = authored_cell(sa, i);
            if (!cellA.valid) { atomicAdd(&narrowTelemetry[16], 1u); break; }
            compoundPoseA = authored_cell_pose(a, sa, cellA);
            compoundShapeA.dimensions_type = vec4<f32>(cellA.maximum - cellA.minimum, 2.0);
        }
        let aCenter = contact_root_point(b, sb, body_position_in_frame(a, a), a);
        // A sphere encloses the rotated A child in B root space for BVH pruning.
        let reach = .5 * length(compoundShapeA.dimensions_type.xyz) + currentSpeculativeDistance;
        var nodeIndex = 0u;
        loop {
            if (nodeIndex >= sb.node_count) { break; }
            let node = authored_node(sb, nodeIndex);
            if (!node.valid) { atomicAdd(&narrowTelemetry[16], 1u); break; }
            let delta = aCenter - clamp(aCenter, node.minimum, node.maximum);
            if (dot(delta, delta) > reach * reach) { nodeIndex = node.escape; continue; }
            nodeIndex++;
            if (node.cell == SENTINEL) { continue; }
            let cellB = authored_cell(sb, node.cell);
            if (!cellB.valid) { atomicAdd(&narrowTelemetry[16], 1u); break; }
            compoundPoseB = authored_cell_pose(b, sb, cellB);
            compoundShapeB.dimensions_type = vec4<f32>(cellB.maximum - cellB.minimum, 2.0);
            var hit = empty_candidates();
            let categoryA = canonical_shape(compoundShapeA.dimensions_type.w);
            if (categoryA == 2u) {
                hit = collide_authored_box_cells(a, b, sa, sb, cellA, cellB);
            } else {
                hit = collide_lego_polyhedron_parts(a, categoryA, b, 2u);
            }
            var exterior = empty_candidates(); exterior.normal = hit.normal;
            for (var k = 0u; k < hit.count; k++) {
                let c = hit.items[k];
                var featureA = c.features.x | (i << 12u);
                if (sa.valid) { featureA = authored_contact_feature(a, sa, cellA,
                    c.pointA_separation.xyz, hit.normal, a); }
                let featureB = authored_contact_feature(b, sb, cellB, c.pointB.xyz, -hit.normal, a);
                if (featureA == SENTINEL || featureB == SENTINEL) { continue; }
                append_candidate(&exterior, c.pointA_separation.xyz, c.pointB.xyz,
                    c.pointA_separation.w, featureA, featureB);
            }
            append_lego_contacts(&result, &deepest, exterior, 0u, 0u);
        }
    }
    compoundEnabled = false;
    return result;
}

fn collide_authored_polyhedra(a: u32, b: u32) -> CandidateSet {
    if (body_has_authored(b)) { return collide_authored_polyhedra_ordered(a, b); }
    // Ordered results use B's sector, so shift their points back to A's sector.
    var result = swap_candidates(collide_authored_polyhedra_ordered(b, a));
    let delta = body_position_in_frame(b, a) - poses[b].position_invMass.xyz;
    for (var k = 0u; k < result.count; k++) {
        result.items[k].pointA_separation = vec4<f32>(result.items[k].pointA_separation.xyz + delta,
            result.items[k].pointA_separation.w);
        result.items[k].pointB = vec4<f32>(result.items[k].pointB.xyz + delta, 0.0);
    }
    return result;
}

fn pair_has_lego(pair : KeyValue) -> bool {
    return legoIsBrick(bitcast<u32>(shapes[pair.keyHigh].invInertia_material.w))
        || legoIsBrick(bitcast<u32>(shapes[pair.keyLow].invInertia_material.w));
}

fn write_class_manifold(pairRecord : KeyValue, candidates : CandidateSet) {
    if (pairRecord.ordinal >= narrow.capacities.z) { return; }
    currentManifolds[pairRecord.ordinal] = build_manifold(pairRecord, candidates);
}

fn narrow_sphere_sphere_impl(gid : vec3<u32>) {
    let pairRecord = class_pair_record(gid, 0u);
    if (pairRecord.ordinal >= narrow.capacities.z) { return; }
    write_class_manifold(pairRecord, collide_sphere_sphere(
        pairRecord.keyHigh, pairRecord.keyLow, pairRecord.keyHigh));
}

fn narrow_sphere_capsule_impl(gid : vec3<u32>) {
    let pairRecord = class_pair_record(gid, 1u);
    if (pairRecord.ordinal >= narrow.capacities.z) { return; }
    let bodyA = pairRecord.keyHigh;
    let bodyB = pairRecord.keyLow;
    if (canonical_shape(compound_shape(bodyA).dimensions_type.w) == 0u) {
        write_class_manifold(pairRecord, collide_sphere_capsule(
            bodyA, bodyB, bodyA));
        return;
    }
    write_class_manifold(pairRecord, swap_candidates(
        collide_sphere_capsule(bodyB, bodyA, bodyA)));
}

fn narrow_capsule_capsule_impl(gid : vec3<u32>) {
    let pairRecord = class_pair_record(gid, 2u);
    if (pairRecord.ordinal >= narrow.capacities.z) { return; }
    write_class_manifold(pairRecord, collide_capsule_capsule(
        pairRecord.keyHigh, pairRecord.keyLow, pairRecord.keyHigh));
}

fn narrow_sphere_box_impl(gid : vec3<u32>) {
    let pairRecord = class_pair_record(gid, 3u);
    if (pairRecord.ordinal >= narrow.capacities.z) { return; }
    if (pair_has_authored(pairRecord) != AUTHORED_PAIR_PASS) { return; }
    if (AUTHORED_PAIR_PASS) {
        let a = pairRecord.keyHigh; let b = pairRecord.keyLow;
        if (body_has_authored(b)) { write_class_manifold(pairRecord, collide_authored_round(a, b, a, false)); }
        else { write_class_manifold(pairRecord, swap_candidates(collide_authored_round(b, a, a, false))); }
        return;
    }
    if (pair_has_lego(pairRecord)) {
        write_class_manifold(pairRecord,
            collide_lego_sphere(pairRecord.keyHigh, pairRecord.keyLow));
        return;
    }
    let bodyA = pairRecord.keyHigh;
    let bodyB = pairRecord.keyLow;
    if (canonical_shape(compound_shape(bodyA).dimensions_type.w) == 0u) {
        write_class_manifold(pairRecord, collide_sphere_box(
            bodyA, bodyB, bodyA));
        return;
    }
    write_class_manifold(pairRecord, swap_candidates(
        collide_sphere_box(bodyB, bodyA, bodyA)));
}

fn narrow_capsule_box_impl(gid : vec3<u32>) {
    let pairRecord = class_pair_record(gid, 4u);
    if (pairRecord.ordinal >= narrow.capacities.z) { return; }
    if (pair_has_authored(pairRecord) != AUTHORED_PAIR_PASS) { return; }
    if (AUTHORED_PAIR_PASS) {
        let a = pairRecord.keyHigh; let b = pairRecord.keyLow;
        if (body_has_authored(b)) { write_class_manifold(pairRecord, collide_authored_round(a, b, a, true)); }
        else { write_class_manifold(pairRecord, swap_candidates(collide_authored_round(b, a, a, true))); }
        return;
    }
    if (pair_has_lego(pairRecord)) {
        write_class_manifold(pairRecord,
            collide_lego_capsule(pairRecord.keyHigh, pairRecord.keyLow));
        return;
    }
    let bodyA = pairRecord.keyHigh;
    let bodyB = pairRecord.keyLow;
    if (canonical_shape(compound_shape(bodyA).dimensions_type.w) == 1u) {
        write_class_manifold(pairRecord, collide_capsule_box(
            bodyA, bodyB, bodyA));
        return;
    }
    write_class_manifold(pairRecord, swap_candidates(
        collide_capsule_box(bodyB, bodyA, bodyA)));
}

fn narrow_box_box_impl(gid : vec3<u32>) {
    let pairRecord = class_pair_record(gid, 5u);
    if (pairRecord.ordinal >= narrow.capacities.z) { return; }
    if (pair_has_authored(pairRecord) != AUTHORED_PAIR_PASS) { return; }
    if (AUTHORED_PAIR_PASS) {
        write_class_manifold(pairRecord, collide_authored_polyhedra(pairRecord.keyHigh, pairRecord.keyLow));
        return;
    }
    if (pair_has_lego(pairRecord)) {
        write_class_manifold(pairRecord,
            collide_lego_polyhedra(pairRecord.keyHigh, pairRecord.keyLow));
        return;
    }
    write_class_manifold(pairRecord, collide_box_box(
        pairRecord.keyHigh, pairRecord.keyLow, pairRecord.keyHigh));
}

fn narrow_sphere_cylinder_impl(gid : vec3<u32>) {
    let pairRecord = class_pair_record(gid, 6u);
    if (pairRecord.ordinal >= narrow.capacities.z) { return; }
    let bodyA = pairRecord.keyHigh;
    let bodyB = pairRecord.keyLow;
    if (canonical_shape(compound_shape(bodyA).dimensions_type.w) == 0u) {
        write_class_manifold(pairRecord, collide_sphere_cylinder(
            bodyA, bodyB, bodyA));
        return;
    }
    write_class_manifold(pairRecord, swap_candidates(
        collide_sphere_cylinder(bodyB, bodyA, bodyA)));
}

fn narrow_capsule_cylinder_impl(gid : vec3<u32>) {
    let pairRecord = class_pair_record(gid, 7u);
    if (pairRecord.ordinal >= narrow.capacities.z) { return; }
    let bodyA = pairRecord.keyHigh;
    let bodyB = pairRecord.keyLow;
    if (canonical_shape(compound_shape(bodyA).dimensions_type.w) == 1u) {
        write_class_manifold(pairRecord, collide_capsule_cylinder(
            bodyA, bodyB, bodyA));
        return;
    }
    write_class_manifold(pairRecord, swap_candidates(
        collide_capsule_cylinder(bodyB, bodyA, bodyA)));
}

fn narrow_box_cylinder_impl(gid : vec3<u32>) {
    let pairRecord = class_pair_record(gid, 8u);
    if (pairRecord.ordinal >= narrow.capacities.z) { return; }
    if (pair_has_authored(pairRecord) != AUTHORED_PAIR_PASS) { return; }
    if (AUTHORED_PAIR_PASS) {
        write_class_manifold(pairRecord, collide_authored_polyhedra(pairRecord.keyHigh, pairRecord.keyLow));
        return;
    }
    if (pair_has_lego(pairRecord)) {
        write_class_manifold(pairRecord,
            collide_lego_polyhedra(pairRecord.keyHigh, pairRecord.keyLow));
        return;
    }
    let bodyA = pairRecord.keyHigh;
    let bodyB = pairRecord.keyLow;
    write_class_manifold(pairRecord, collide_polyhedra(
        bodyA,
        canonical_shape(compound_shape(bodyA).dimensions_type.w),
        bodyB,
        canonical_shape(compound_shape(bodyB).dimensions_type.w),
        bodyA));
}

fn narrow_cylinder_cylinder_impl(gid : vec3<u32>) {
    let pairRecord = class_pair_record(gid, 9u);
    if (pairRecord.ordinal >= narrow.capacities.z) { return; }
    let bodyA = pairRecord.keyHigh;
    let bodyB = pairRecord.keyLow;
    write_class_manifold(pairRecord, collide_polyhedra(
        bodyA,
        canonical_shape(compound_shape(bodyA).dimensions_type.w),
        bodyB,
        canonical_shape(compound_shape(bodyB).dimensions_type.w),
        bodyA));
}

@compute @workgroup_size(1)
fn finalize_narrow(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    atomicStore(&narrowTelemetry[22], min(broadTelemetry[3],
        min(narrow.capacities.y, narrow.capacities.z)));
    let tick = atomicLoad(&narrowTelemetry[23]) + 1u;
    atomicStore(&narrowTelemetry[23], tick);
    atomicStore(&narrowTelemetry[21], tick);
}

@compute @workgroup_size(64)
fn narrow_sphere_sphere_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_sphere_sphere_impl(gid);
}
@compute @workgroup_size(128)
fn narrow_sphere_sphere_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_sphere_sphere_impl(gid);
}
@compute @workgroup_size(256)
fn narrow_sphere_sphere_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_sphere_sphere_impl(gid);
}

@compute @workgroup_size(64)
fn narrow_sphere_capsule_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_sphere_capsule_impl(gid);
}
@compute @workgroup_size(128)
fn narrow_sphere_capsule_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_sphere_capsule_impl(gid);
}
@compute @workgroup_size(256)
fn narrow_sphere_capsule_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_sphere_capsule_impl(gid);
}

@compute @workgroup_size(64)
fn narrow_capsule_capsule_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_capsule_capsule_impl(gid);
}
@compute @workgroup_size(128)
fn narrow_capsule_capsule_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_capsule_capsule_impl(gid);
}
@compute @workgroup_size(256)
fn narrow_capsule_capsule_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_capsule_capsule_impl(gid);
}

@compute @workgroup_size(64)
fn narrow_sphere_box_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_sphere_box_impl(gid);
}
@compute @workgroup_size(128)
fn narrow_sphere_box_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_sphere_box_impl(gid);
}
@compute @workgroup_size(256)
fn narrow_sphere_box_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_sphere_box_impl(gid);
}

@compute @workgroup_size(64)
fn narrow_capsule_box_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_capsule_box_impl(gid);
}
@compute @workgroup_size(128)
fn narrow_capsule_box_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_capsule_box_impl(gid);
}
@compute @workgroup_size(256)
fn narrow_capsule_box_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_capsule_box_impl(gid);
}

@compute @workgroup_size(64)
fn narrow_box_box_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_box_box_impl(gid);
}
@compute @workgroup_size(128)
fn narrow_box_box_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_box_box_impl(gid);
}
@compute @workgroup_size(256)
fn narrow_box_box_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_box_box_impl(gid);
}

@compute @workgroup_size(64)
fn narrow_sphere_cylinder_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_sphere_cylinder_impl(gid);
}
@compute @workgroup_size(128)
fn narrow_sphere_cylinder_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_sphere_cylinder_impl(gid);
}
@compute @workgroup_size(256)
fn narrow_sphere_cylinder_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_sphere_cylinder_impl(gid);
}

@compute @workgroup_size(64)
fn narrow_capsule_cylinder_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_capsule_cylinder_impl(gid);
}
@compute @workgroup_size(128)
fn narrow_capsule_cylinder_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_capsule_cylinder_impl(gid);
}
@compute @workgroup_size(256)
fn narrow_capsule_cylinder_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_capsule_cylinder_impl(gid);
}

@compute @workgroup_size(64)
fn narrow_box_cylinder_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_box_cylinder_impl(gid);
}
@compute @workgroup_size(128)
fn narrow_box_cylinder_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_box_cylinder_impl(gid);
}
@compute @workgroup_size(256)
fn narrow_box_cylinder_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_box_cylinder_impl(gid);
}

@compute @workgroup_size(64)
fn narrow_cylinder_cylinder_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_cylinder_cylinder_impl(gid);
}
@compute @workgroup_size(128)
fn narrow_cylinder_cylinder_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_cylinder_cylinder_impl(gid);
}
@compute @workgroup_size(256)
fn narrow_cylinder_cylinder_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    narrow_cylinder_cylinder_impl(gid);
}
