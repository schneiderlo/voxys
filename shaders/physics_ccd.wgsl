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

const BODY_ALIVE : u32 = 1u << 20u;
const BODY_AWAKE : u32 = 1u << 21u;
const BODY_BULLET : u32 = 1u << 22u;
const BODY_CCD_HIT : u32 = 1u << 23u;
const BODY_CCD_FAILURE : u32 = 1u << 24u;
const SHAPE_SPHERE : u32 = 0u;
const SHAPE_CAPSULE : u32 = 3u;
const WORLD_SECTOR_SIZE : f32 = 256.0;
const WORLD_SECTOR_HALF : f32 = 128.0;

struct BodyPose {
    position_invMass : vec4<f32>,
    orientation : vec4<f32>,
};

struct BodyMotion {
    linearVelocity_sleep : vec4<f32>,
    angularVelocity_flags : vec4<f32>,
};

struct BodyShape {
    dimensions_type : vec4<f32>,
    invInertia_material : vec4<f32>,
    material_coefficients : vec4<f32>,
    authored_shape : vec4<u32>,
};

struct CcdParams {
    counts : vec4<u32>,
    terrain : vec4<u32>,
    terrainOrigin_cell_height : vec4<f32>,
    tuning : vec4<f32>,
    worldSector : vec4<i32>,
};

struct TerrainSurface {
    height : f32,
    normal : vec3<f32>,
    valid : bool,
};

struct Clearance {
    distance : f32,
    normal : vec3<f32>,
    valid : bool,
};

struct SweepResult {
    hit : bool,
    fraction : f32,
    normal : vec3<f32>,
    iterations : u32,
};

@group(0) @binding(0) var<storage, read_write> poses : array<BodyPose>;
@group(0) @binding(1) var<storage, read_write> motions : array<BodyMotion>;
@group(0) @binding(2) var<storage, read> shapes : array<BodyShape>;
@group(0) @binding(3) var<storage, read_write> metadata : array<vec4<i32>>;
@group(0) @binding(4) var<storage, read_write> bodyValues : array<u32>;
@group(0) @binding(5) var<storage, read_write> bulletPredicates : array<u32>;
@group(0) @binding(6) var<storage, read> bulletIds : array<u32>;
@group(0) @binding(7) var<storage, read> compactResult : array<u32>;
@group(0) @binding(8) var<storage, read_write> telemetry : array<atomic<u32>>;
@group(0) @binding(9) var<uniform> ccd : CcdParams;
@group(0) @binding(10) var maxHeightTexture : texture_2d<u32>;
@group(0) @binding(11) var<storage, read> authored_shape_heap: array<vec4<u32>>;
@group(0) @binding(12) var<storage, read> static_authored_bodies: array<vec2<u32>>;

fn quaternion_rotate(q : vec4<f32>, value : vec3<f32>) -> vec3<f32> {
    let twiceCross = 2.0 * cross(q.xyz, value);
    return value + q.w * twiceCross + cross(q.xyz, twiceCross);
}

fn saturating_sector_step(value : i32, step : i32) -> i32 {
    if (step > 0 && value > 2147483647 - step) { return 2147483647; }
    if (step < 0 && value < -2147483647 - 1 - step) {
        return -2147483647 - 1;
    }
    return value + step;
}

fn normalize_world_position(pose : ptr<function, BodyPose>,
                            worldMeta : ptr<function, vec4<i32>>) {
    for (var axis = 0u; axis < 3u; axis += 1u) {
        let local = (*pose).position_invMass[axis];
        let step = i32(floor((local + 128.0) / 256.0));
        if (step == 0) { continue; }
        let oldSector = (*worldMeta)[axis];
        let newSector = saturating_sector_step(oldSector, step);
        if (newSector == oldSector) {
            (*pose).position_invMass[axis] = clamp(
                local, -128.0, bitcast<f32>(bitcast<u32>(128.0) - 1u));
        } else {
            (*worldMeta)[axis] = newSector;
            (*pose).position_invMass[axis] = local - f32(step) * 256.0;
        }
    }
}

fn bounded_sector_delta(reference : i32, other : i32,
                        maximum : u32) -> i32 {
    if (other >= reference) {
        let wide = bitcast<u32>(other) - bitcast<u32>(reference);
        if (wide > maximum) { return 2147483647; }
        return i32(wide);
    }
    let wide = bitcast<u32>(reference) - bitcast<u32>(other);
    if (wide > maximum) { return 2147483647; }
    return -i32(wide);
}

fn terrain_frame_pose(pose : BodyPose, worldMeta : vec4<i32>,
                      shapeRadius : f32,
                      valid : ptr<function, bool>) -> BodyPose {
    let width = f32(max(ccd.terrain.x, 1u) - 1u);
    let height = f32(max(ccd.terrain.y, 1u) - 1u);
    let horizontalRadius = u32(ceil((
        0.5 * max(width, height) * ccd.terrainOrigin_cell_height.z
        + shapeRadius) / WORLD_SECTOR_SIZE)) + 1u;
    let verticalRadius = u32(ceil((
        abs(ccd.terrainOrigin_cell_height.w) + shapeRadius)
        / WORLD_SECTOR_SIZE)) + 1u;
    let delta = vec3<i32>(
        bounded_sector_delta(
            ccd.worldSector.x, worldMeta.x, horizontalRadius),
        bounded_sector_delta(
            ccd.worldSector.y, worldMeta.y, verticalRadius),
        bounded_sector_delta(
            ccd.worldSector.z, worldMeta.z, horizontalRadius));
    let validValue = all(delta != vec3<i32>(2147483647));
    (*valid) = validValue;
    var result = pose;
    if (validValue) {
        result.position_invMass = vec4<f32>(
            pose.position_invMass.xyz
                + vec3<f32>(delta) * WORLD_SECTOR_SIZE,
            pose.position_invMass.w);
    }
    return result;
}

fn raw_height_to_world(rawHeight : f32) -> f32 {
    return ccd.terrainOrigin_cell_height.w
        * (2.0 * rawHeight / 65535.0 - 1.0);
}

fn terrain_surface(worldXZ : vec2<f32>) -> TerrainSurface {
    var result = TerrainSurface(0.0, vec3<f32>(0.0, 1.0, 0.0), false);
    let local = (worldXZ + ccd.terrainOrigin_cell_height.xy)
        / ccd.terrainOrigin_cell_height.z;
    let maximum = vec2<f32>(ccd.terrain.xy - vec2<u32>(1u));
    if (any(local < vec2<f32>(0.0)) || any(local > maximum)) {
        return result;
    }
    let cell = min(vec2<u32>(floor(local)), ccd.terrain.xy - vec2<u32>(2u));
    let fraction = clamp(local - vec2<f32>(cell),
                         vec2<f32>(0.0), vec2<f32>(1.0));
    let coordinate = vec2<i32>(cell);
    let topLeft = raw_height_to_world(f32(textureLoad(
        maxHeightTexture, coordinate, 0).x));
    let topRight = raw_height_to_world(f32(textureLoad(
        maxHeightTexture, coordinate + vec2<i32>(1, 0), 0).x));
    let bottomLeft = raw_height_to_world(f32(textureLoad(
        maxHeightTexture, coordinate + vec2<i32>(0, 1), 0).x));
    let bottomRight = raw_height_to_world(f32(textureLoad(
        maxHeightTexture, coordinate + vec2<i32>(1, 1), 0).x));
    var gradient = vec2<f32>(0.0);
    if (fraction.y >= fraction.x) {
        result.height = topLeft
            + fraction.x * (bottomRight - bottomLeft)
            + fraction.y * (bottomLeft - topLeft);
        gradient = vec2<f32>(bottomRight - bottomLeft,
                             bottomLeft - topLeft);
    } else {
        result.height = topLeft
            + fraction.x * (topRight - topLeft)
            + fraction.y * (bottomRight - topRight);
        gradient = vec2<f32>(topRight - topLeft,
                             bottomRight - topRight);
    }
    gradient /= ccd.terrainOrigin_cell_height.z;
    result.normal = normalize(vec3<f32>(-gradient.x, 1.0, -gradient.y));
    result.valid = true;
    return result;
}

fn shape_bounding_radius(shape : BodyShape) -> f32 {
    let dimensions = max(abs(shape.dimensions_type.xyz), vec3<f32>(1e-5));
    let shapeType = u32(clamp(shape.dimensions_type.w, 0.0, 4.0));
    if (shapeType == SHAPE_SPHERE) { return 0.5 * dimensions.x; }
    if (shapeType == SHAPE_CAPSULE) {
        let radius = 0.25 * (dimensions.x + dimensions.z);
        return max(0.5 * dimensions.y, radius);
    }
    return 0.5 * length(dimensions);
}

fn point_clearance(point : vec3<f32>, radius : f32) -> Clearance {
    if (ccd.terrain.w == 2u) {
        let contact = legoSphereContact(maxHeightTexture,ccd.terrainOrigin_cell_height,ccd.terrain.xy,point,radius);
        return Clearance(contact.distance,contact.normal,contact.feature!=0u);
    }
    let surface = terrain_surface(point.xz);
    if (!surface.valid) {
        return Clearance(1e30, vec3<f32>(0.0, 1.0, 0.0), false);
    }
    return Clearance(point.y - surface.height - radius,
                     surface.normal, true);
}

fn shape_clearance(center : vec3<f32>, pose : BodyPose,
                   shape : BodyShape) -> Clearance {
    let shapeType = u32(clamp(shape.dimensions_type.w, 0.0, 4.0));
    let material = bitcast<u32>(shape.invInertia_material.w);
    if (legoIsBrick(material)) {
        // The enclosing sphere is only a broad-phase bound. Treating it as
        // the brick's collision surface leaves a 2x1 brick hovering ~0.67
        // units above flat ground and prevents the real contact solver running.
        var best = Clearance(1e20, vec3<f32>(0,1,0), false);
        for (var part=0u; part<legoBrickParts(shape.dimensions_type.xyz,material); part++) {
            let offset = legoBrickPartOffset(shape.dimensions_type.xyz,part);
            let size = legoBrickPartSize(shape.dimensions_type.xyz,part);
            var sample = point_clearance(center+quaternion_rotate(pose.orientation,offset),0.0);
            if (!sample.valid) { continue; }
            let localNormal = quaternion_rotate(vec4<f32>(-pose.orientation.xyz,pose.orientation.w),sample.normal);
            var extent = dot(abs(localNormal),size*0.5);
            if (part>0u) { extent = length(localNormal.xz)*size.x*0.5+abs(localNormal.y)*size.y*0.5; }
            sample.distance -= extent;
            if (sample.distance < best.distance) { best = sample; }
        }
        return best;
    }
    if (shapeType != SHAPE_CAPSULE) {
        return point_clearance(center, shape_bounding_radius(shape));
    }
    let dimensions = max(abs(shape.dimensions_type.xyz), vec3<f32>(1e-5));
    let radius = 0.25 * (dimensions.x + dimensions.z);
    let segmentHalf = max(0.5 * dimensions.y - radius, 0.0);
    let axis = quaternion_rotate(
        pose.orientation, vec3<f32>(0.0, segmentHalf, 0.0));
    var best = point_clearance(center - axis, radius);
    let middle = point_clearance(center, radius);
    if (middle.distance < best.distance) { best = middle; }
    let top = point_clearance(center + axis, radius);
    if (top.distance < best.distance) { best = top; }
    return best;
}

fn sweep_terrain(pose : BodyPose, shape : BodyShape,
                 translation : vec3<f32>) -> SweepResult {
    var result = SweepResult(false, 1.0, vec3<f32>(0.0, 1.0, 0.0), 0u);
    var previousFraction = 0.0;
    var previous = shape_clearance(
        pose.position_invMass.xyz, pose, shape);
    if (previous.valid && previous.distance <= ccd.tuning.z) {
        if (legoIsBrick(bitcast<u32>(shape.invInertia_material.w))
            && dot(translation,previous.normal)>=0.0) { return result; }
        result.hit = true;
        result.fraction = 0.0;
        result.normal = previous.normal;
        return result;
    }
    if (ccd.terrain.w == 2u) {
        // The contact query includes a one-cell halo. Capping advancement to
        // half a cell keeps even a tall, previously unvisited step conservative.
        // Exhaustion stops at the last safe pose instead of tunnelling.
        let travel = length(translation);
        if (travel < 1e-8) { return result; }
        var fraction = 0.0;
        var normal = vec3<f32>(0.0,1.0,0.0);
        for (var iteration=0u; iteration<256u; iteration+=1u) {
            let sample = shape_clearance(pose.position_invMass.xyz+translation*fraction,pose,shape);
            result.iterations += 1u;
            normal = sample.normal;
            if (sample.valid && sample.distance <= ccd.tuning.z) {
                return SweepResult(true,fraction,normal,result.iterations);
            }
            let advance = min(0.5*ccd.terrainOrigin_cell_height.z,
                max((sample.distance-ccd.tuning.z)*0.9,1e-5));
            if (fraction + advance/travel >= 1.0) {
                let end = shape_clearance(pose.position_invMass.xyz+translation,pose,shape);
                if (end.valid && end.distance <= ccd.tuning.z) {
                    return SweepResult(true,fraction,normal,result.iterations);
                }
                return result;
            }
            fraction += advance/travel;
        }
        return SweepResult(true,fraction,normal,result.iterations);
    }
    let coarseSteps = max(ccd.counts.z, 1u);
    for (var step = 1u; step <= 32u; step += 1u) {
        if (step > coarseSteps) { break; }
        let fraction = f32(step) / f32(coarseSteps);
        let sample = shape_clearance(
            pose.position_invMass.xyz + translation * fraction, pose, shape);
        result.iterations += 1u;
        if (sample.valid && sample.distance <= ccd.tuning.z) {
            var lower = previousFraction;
            var upper = fraction;
            var hitClearance = sample;
            for (var iteration = 0u; iteration < 16u; iteration += 1u) {
                if (iteration >= ccd.terrain.z) { break; }
                let middleFraction = 0.5 * (lower + upper);
                let middle = shape_clearance(
                    pose.position_invMass.xyz
                        + translation * middleFraction, pose, shape);
                result.iterations += 1u;
                if (middle.valid && middle.distance <= ccd.tuning.z) {
                    upper = middleFraction;
                    hitClearance = middle;
                } else {
                    lower = middleFraction;
                }
            }
            result.hit = true;
            result.fraction = upper;
            result.normal = hitClearance.normal;
            return result;
        }
        previousFraction = fraction;
        previous = sample;
    }
    return result;
}

// Earliest sphere/exterior contact via monotone prefix distance. Unlike
// uniformly sampling a trajectory, a prefix segment cannot skip a thin face.
// The BVH rejects distant cells and only union exterior patches participate.
fn sweep_authored_static(body: u32, pose: BodyPose, translation: vec3<f32>,
                          radius: f32, maximum: f32) -> SweepResult {
    var result = SweepResult(false, maximum, vec3<f32>(0,1,0), 0u);
    // Terrain-only scenes never scan body slots. The compact membership list
    // contains only static authored bodies, whose metadata cannot be modified
    // by another CCD invocation; dynamic transforms/flags are never sampled.
    for (var entry=0u; entry<u32(ccd.worldSector.w); entry++) {
        let identity=static_authored_bodies[entry];
        let obstacle=identity.x;
        if (obstacle>=ccd.counts.x) { continue; }
        if (obstacle == body || shapes[obstacle].authored_shape.x == 0u) { continue; }
        let flags = u32(metadata[obstacle].w);
        if ((flags & 0x000fffffu) != identity.y || (flags & BODY_ALIVE) == 0u
            || (flags & 0x80000000u) != 0u
            || poses[obstacle].position_invMass.w != 0.0) { continue; }
        let geometry = authored_shape_ref(shapes[obstacle].authored_shape);
        if (!geometry.valid) { continue; }
        let reach = u32(ceil((length(translation)+radius+
            geometry.inverse_inertia_radius.w)/WORLD_SECTOR_SIZE))+2u;
        let delta = vec3<i32>(
            bounded_sector_delta(metadata[obstacle].x,metadata[body].x,reach),
            bounded_sector_delta(metadata[obstacle].y,metadata[body].y,reach),
            bounded_sector_delta(metadata[obstacle].z,metadata[body].z,reach));
        if (any(delta == vec3<i32>(2147483647))) { continue; }
        let targetPose=poses[obstacle];
        let inverse=authored_quat_conjugate(targetPose.orientation);
        let start=authored_root_point(geometry,quaternion_rotate(inverse,
            pose.position_invMass.xyz+vec3<f32>(delta)*WORLD_SECTOR_SIZE-targetPose.position_invMass.xyz));
        let travel=authored_root_vector(geometry,quaternion_rotate(inverse,translation));
        // Land a tiny distance inside the surface so a legal zero speculative
        // distance still produces a real narrow-phase contact and solver hit.
        let contactRadius=max(radius-min(0.0001,radius*0.001),0.000001);
        let padding=vec3<f32>(radius);
        let broad=authored_ray_interval(geometry.minimum-padding,
            geometry.maximum+padding,start,travel,result.fraction);
        if (broad.x>broad.y) { continue; }
        var nodeIndex=0u;
        while (nodeIndex<geometry.node_count) {
            let node=authored_node(geometry,nodeIndex);
            if (!node.valid) { break; }
            let range=authored_ray_interval(node.minimum-padding,node.maximum+padding,
                start,travel,result.fraction);
            if (range.x>range.y) { nodeIndex=node.escape; continue; }
            nodeIndex++;
            if (node.cell==0xffffffffu) { continue; }
            let cell=authored_cell(geometry,node.cell);
            for (var f=0u; f<cell.face_count; f++) {
                let face=authored_face(geometry,cell.first_face+f);
                if (!face.valid) { continue; }
                let initial=start-clamp(start,face.minimum,face.maximum);
                // Squared distance to this convex rectangle is convex along
                // the ray. If already touching and separating, this face can
                // never be a later entry. Other faces in this SAME compound
                // must still be tested (e.g. leaving one side of a doorway).
                if (dot(initial,initial)<=radius*radius+1e-7
                    && dot(initial,travel)>=0.0) { continue; }
                let entire=authored_segment_box(start,start+travel*result.fraction,
                    face.minimum,face.maximum);
                if (entire.squared>contactRadius*contactRadius) { continue; }
                var lower=0.0; var upper=result.fraction;
                for (var iteration=0u; iteration<22u; iteration++) {
                    let middle=0.5*(lower+upper);
                    let sample=authored_segment_box(start,start+travel*middle,
                        face.minimum,face.maximum);
                    if (sample.squared<=contactRadius*contactRadius) { upper=middle; }
                    else { lower=middle; }
                }
                let center=start+travel*upper;
                let offset=center-clamp(center,face.minimum,face.maximum);
                var normal=vec3<f32>(0); normal[face.axis]=f32(face.sign);
                if (dot(offset,offset)>1e-16) { normal=normalize(offset); }
                result=SweepResult(true,upper,quaternion_rotate(targetPose.orientation,
                    authored_body_vector(geometry,normal)),22u);
            }
        }
    }
    return result;
}

fn mark_bullets_impl(gid : vec3<u32>) {
    let body = gid.x;
    if (body >= ccd.counts.x) { return; }
    bodyValues[body] = body;
    let flags = u32(metadata[body].w);
    bulletPredicates[body] = select(0u, 1u,
        (flags & (BODY_ALIVE | BODY_AWAKE)) == (BODY_ALIVE | BODY_AWAKE)
        && (flags & BODY_BULLET) != 0u);
    metadata[body].w = bitcast<i32>(
        flags & ~(BODY_CCD_HIT | BODY_CCD_FAILURE));
}

@compute @workgroup_size(64)
fn mark_bullets_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_bullets_impl(gid);
}
@compute @workgroup_size(128)
fn mark_bullets_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_bullets_impl(gid);
}
@compute @workgroup_size(256)
fn mark_bullets_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_bullets_impl(gid);
}

@compute @workgroup_size(1)
fn prepare_ccd(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    for (var word = 0u; word < 8u; word += 1u) {
        atomicStore(&telemetry[word], 0u);
    }
    let requested = compactResult[0];
    atomicStore(&telemetry[1], requested);
    atomicStore(&telemetry[2], min(requested, ccd.counts.y));
    atomicStore(&telemetry[3], requested - min(requested, ccd.counts.y));
    atomicAdd(&telemetry[14], 1u);
}

fn process_body(body : u32, bullet : bool) {
    let flags = u32(metadata[body].w);
    if ((flags & (BODY_ALIVE | BODY_AWAKE))
        != (BODY_ALIVE | BODY_AWAKE)) { return; }
    if (poses[body].position_invMass.w <= 0.0) { return; }
    let shape = shapes[body];
    let shapeType = u32(clamp(shape.dimensions_type.w, 0.0, 4.0));
    if (!bullet && shapeType != SHAPE_SPHERE
        && shapeType != SHAPE_CAPSULE) { return; }
    var pose = poses[body];
    var motion = motions[body];
    let translation = motion.linearVelocity_sleep.xyz * ccd.tuning.x;
    let distance = length(translation);
    let radius = shape_bounding_radius(shape);
    let brick = legoIsBrick(bitcast<u32>(shape.invInertia_material.w));
    let dimensions = abs(shape.dimensions_type.xyz);
    // Ordinary contact solving owns slow/resting bricks. CCD is needed once
    // a tick can travel through a substantial fraction of the thinnest side.
    let terrainCandidate=(bullet || distance>ccd.tuning.y*radius)
        && (!brick || distance>0.5*min(dimensions.x,min(dimensions.y,dimensions.z)));
    let authoredCandidate=ccd.worldSector.w>0 && shapeType==SHAPE_SPHERE
        && shape.authored_shape.x==0u;
    if (!terrainCandidate && !authoredCandidate) { return; }
    if (!bullet && terrainCandidate) { atomicAdd(&telemetry[0], 1u); }
    if (distance <= 1e-8) { return; }
    var terrainFrameValid = false;
    let terrainPose = terrain_frame_pose(
        pose, metadata[body], radius, &terrainFrameValid);

    let underResolved = ccd.terrain.w != 0u && distance > f32(ccd.counts.z)
                     * ccd.terrainOrigin_cell_height.z;
    if (underResolved) {
        atomicAdd(&telemetry[5], 1u);
    }
    var sweep = SweepResult(false, 1.0, vec3<f32>(0,1,0), 0u);
    if (terrainCandidate && terrainFrameValid && ccd.terrain.w != 0u) {
        sweep = sweep_terrain(terrainPose, shape, translation);
    }
    // Only primitive spheres against immobile authored targets are covered.
    // Targets cannot be concurrently updated in this dispatch (zero inverse
    // mass and not kinematic); moving obstacle CCD needs relative trajectories.
    var authoredHit = false;
    if (authoredCandidate) {
        let authored = sweep_authored_static(body, pose, translation, radius, sweep.fraction);
        if (authored.hit && (!sweep.hit || authored.fraction < sweep.fraction)) {
            sweep = authored;
            authoredHit = true;
        }
    }
    atomicMax(&telemetry[7], sweep.iterations);
    if (!sweep.hit) {
        if (bullet && underResolved) {
            metadata[body].w = bitcast<i32>(
                u32(metadata[body].w) | BODY_CCD_FAILURE);
            atomicAdd(&telemetry[6], 1u);
        }
        return;
    }
    let safeFraction = select(max(
        sweep.fraction - ccd.tuning.z / max(distance, 1e-8), 0.0),
        sweep.fraction, authoredHit);
    pose.position_invMass = vec4<f32>(
        pose.position_invMass.xyz + translation * safeFraction,
        pose.position_invMass.w);
    let velocity = motion.linearVelocity_sleep.xyz;
    let inward = min(dot(velocity, sweep.normal), 0.0);
    let tangent = velocity - sweep.normal * inward;
    if (brick && !authoredHit) {
        // Preserve incoming momentum for terrain restitution. Stop positional
        // integration at the impact; the terrain pass follows the body solver.
        motion.angularVelocity_flags.w = -2.0;
    } else if (authoredHit) {
        // Keep incoming velocity: narrow phase produces a real manifold and
        // the normal GPU solver owns restitution, friction and ContactHit.
        // The dynamic-only spare lane carries consumed tick time. Kinematic
        // bodies use this lane for a command tick and never enter this path.
        motion.angularVelocity_flags.w = -1.0 - safeFraction;
    } else {
        motion.linearVelocity_sleep = vec4<f32>(
            tangent * (1.0 - safeFraction), motion.linearVelocity_sleep.w);
    }
    var worldMeta = metadata[body];
    normalize_world_position(&pose, &worldMeta);
    poses[body] = pose;
    motions[body] = motion;
    worldMeta.w = bitcast<i32>(u32(worldMeta.w) | BODY_CCD_HIT);
    metadata[body] = worldMeta;
    atomicAdd(&telemetry[4], 1u);
}

fn execute_ccd_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index < ccd.counts.x
        && (u32(metadata[index].w) & BODY_BULLET) == 0u) {
        process_body(index, false);
    }
    let bulletCount = atomicLoad(&telemetry[2]);
    if (index < bulletCount) {
        process_body(bulletIds[index], true);
    }
}

@compute @workgroup_size(64)
fn execute_ccd_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    execute_ccd_impl(gid);
}
@compute @workgroup_size(128)
fn execute_ccd_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    execute_ccd_impl(gid);
}
@compute @workgroup_size(256)
fn execute_ccd_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    execute_ccd_impl(gid);
}

@compute @workgroup_size(1)
fn finalize_ccd(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    atomicMax(&telemetry[8], atomicLoad(&telemetry[0]));
    atomicMax(&telemetry[9], atomicLoad(&telemetry[1]));
    atomicMax(&telemetry[10], atomicLoad(&telemetry[3]));
    atomicMax(&telemetry[11], atomicLoad(&telemetry[4]));
    atomicMax(&telemetry[12], atomicLoad(&telemetry[5]));
    atomicMax(&telemetry[13], atomicLoad(&telemetry[6]));
}
