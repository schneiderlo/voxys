const GENERATION_MASK : u32 = 0x000fffffu;
const BODY_ALIVE : u32 = 1u << 20u;
const QUERY_RAY : u32 = 0u;
const QUERY_OVERLAP_SPHERE : u32 = 1u;
const QUERY_SPHERE_CAST : u32 = 2u;
const QUERY_CAPSULE_CAST : u32 = 3u;
const SHAPE_CUBE : u32 = 1u;
const SHAPE_BOX : u32 = 2u;
const MAX_HITS : u32 = 16u;
const SENTINEL : u32 = 0xffffffffu;

struct BodyPose {
    position_invMass : vec4<f32>,
    orientation : vec4<f32>,
};

struct BodyShape {
    dimensions_type : vec4<f32>,
    invInertia_material : vec4<f32>,
};

struct QueryRequest {
    ids : vec4<u32>,
    originRadius : vec4<f32>,
    directionDistance : vec4<f32>,
    dimensions : vec4<f32>,
    sector : vec4<i32>,
};

struct QueryHit {
    ids : vec4<u32>,
    metricDistance : vec4<f32>,
    point : vec4<f32>,
    normal : vec4<f32>,
    sector : vec4<i32>,
};

struct QueryOutput {
    header : vec4<u32>,
    hits : array<QueryHit, 16>,
};

struct QueryParams {
    counts : vec4<u32>,
};

struct Intersection {
    hit : bool,
    distance : f32,
    point : vec3<f32>,
    normal : vec3<f32>,
};

@group(0) @binding(0) var<storage, read> poses : array<BodyPose>;
@group(0) @binding(1) var<storage, read> shapes : array<BodyShape>;
@group(0) @binding(2) var<storage, read> metadata : array<vec4<i32>>;
@group(0) @binding(3) var<storage, read> requests : array<QueryRequest>;
@group(0) @binding(4) var<storage, read_write> outputs : array<QueryOutput>;
@group(0) @binding(5) var<uniform> params : QueryParams;

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

fn body_position_in_query_frame(body : u32,
                                querySector : vec3<i32>,
                                maximum : u32) -> vec3<f32> {
    var sectorDelta = vec3<i32>(0);
    for (var axis = 0u; axis < 3u; axis += 1u) {
        sectorDelta[axis] = bounded_sector_delta(
            querySector[axis], metadata[body][axis], maximum);
        if (sectorDelta[axis] == 2147483647) {
            return vec3<f32>(3.402823466e+38);
        }
    }
    return poses[body].position_invMass.xyz
         + vec3<f32>(sectorDelta) * 256.0;
}

fn quaternion_rotate(q : vec4<f32>, value : vec3<f32>) -> vec3<f32> {
    let twiceCross = 2.0 * cross(q.xyz, value);
    return value + q.w * twiceCross + cross(q.xyz, twiceCross);
}

fn quaternion_inverse_rotate(q : vec4<f32>, value : vec3<f32>) -> vec3<f32> {
    return quaternion_rotate(vec4<f32>(-q.xyz, q.w), value);
}

fn shape_radius(shape : BodyShape) -> f32 {
    let shapeType = u32(clamp(shape.dimensions_type.w, 0.0, 4.0));
    let dimensions = 0.5 * abs(shape.dimensions_type.xyz);
    if (shapeType == 0u) { return dimensions.x; }
    if (shapeType == 3u) {
        let radius = 0.5 * (dimensions.x + dimensions.z);
        return max(dimensions.y, radius);
    }
    return length(dimensions);
}

fn miss() -> Intersection {
    return Intersection(false, 0.0, vec3<f32>(0.0), vec3<f32>(0.0));
}

fn ray_sphere(origin : vec3<f32>, direction : vec3<f32>, maximum : f32,
              center : vec3<f32>, radius : f32) -> Intersection {
    let offset = origin - center;
    let c = dot(offset, offset) - radius * radius;
    if (c <= 0.0) {
        let normal = normalize(select(vec3<f32>(0.0, 1.0, 0.0), offset,
            dot(offset, offset) > 1e-12));
        return Intersection(true, 0.0, origin, normal);
    }
    let b = dot(offset, direction);
    let discriminant = b * b - c;
    if (discriminant < 0.0) { return miss(); }
    let distance = -b - sqrt(discriminant);
    if (distance < 0.0 || distance > maximum) { return miss(); }
    let point = origin + direction * distance;
    return Intersection(true, distance, point, normalize(point - center));
}

fn ray_box(origin : vec3<f32>, direction : vec3<f32>, maximum : f32,
           pose : BodyPose, extents : vec3<f32>) -> Intersection {
    let localOrigin = quaternion_inverse_rotate(
        pose.orientation, origin - pose.position_invMass.xyz);
    let localDirection = quaternion_inverse_rotate(pose.orientation, direction);
    var near = 0.0;
    var far = maximum;
    var nearAxis = 0u;
    var nearSign = 1.0;
    for (var axis = 0u; axis < 3u; axis += 1u) {
        if (abs(localDirection[axis]) < 1e-8) {
            if (localOrigin[axis] < -extents[axis]
                || localOrigin[axis] > extents[axis]) { return miss(); }
            continue;
        }
        let inverseDirection = 1.0 / localDirection[axis];
        var first = (-extents[axis] - localOrigin[axis]) * inverseDirection;
        var second = (extents[axis] - localOrigin[axis]) * inverseDirection;
        var sign = -1.0;
        if (first > second) {
            let temporary = first;
            first = second;
            second = temporary;
            sign = 1.0;
        }
        if (first > near) {
            near = first;
            nearAxis = axis;
            nearSign = sign;
        }
        far = min(far, second);
        if (near > far) { return miss(); }
    }
    if (near > maximum) { return miss(); }
    var localNormal = vec3<f32>(0.0);
    localNormal[nearAxis] = nearSign;
    return Intersection(true, near, origin + direction * near,
        quaternion_rotate(pose.orientation, localNormal));
}

fn hit_less(a : QueryHit, b : QueryHit) -> bool {
    return a.metricDistance.x < b.metricDistance.x
        || (a.metricDistance.x == b.metricDistance.x
            && (a.ids.y < b.ids.y
                || (a.ids.y == b.ids.y && a.ids.z < b.ids.z)));
}

fn insert_hit(query : u32, hit : QueryHit, maximumHits : u32,
              count : ptr<function, u32>, overflow : ptr<function, u32>) {
    if (maximumHits == 0u) {
        *overflow = 1u;
        return;
    }
    let stored = min(*count, maximumHits);
    var insertion = stored;
    for (var index = 0u; index < stored; index += 1u) {
        if (hit_less(hit, outputs[query].hits[index])) {
            insertion = index;
            break;
        }
    }
    if (insertion < maximumHits) {
        var destination = min(stored, maximumHits - 1u);
        while (destination > insertion) {
            outputs[query].hits[destination] =
                outputs[query].hits[destination - 1u];
            destination -= 1u;
        }
        outputs[query].hits[insertion] = hit;
    }
    *count = min(*count + 1u, maximumHits);
    if (stored == maximumHits) { *overflow = 1u; }
}

@compute @workgroup_size(1)
fn execute_queries(@builtin(global_invocation_id) gid : vec3<u32>) {
    let query = gid.x;
    if (query >= params.counts.y) { return; }
    let request = requests[query];
    let queryType = min(request.ids.y, QUERY_CAPSULE_CAST);
    let maximumHits = min(request.ids.z, MAX_HITS);
    outputs[query].header = vec4<u32>(request.ids.x, 0u, 0u, queryType);
    let emptyHit = QueryHit(
        vec4<u32>(request.ids.x, SENTINEL, SENTINEL, queryType),
        vec4<f32>(1e30), vec4<f32>(0.0), vec4<f32>(0.0),
        request.sector);
    for (var hitIndex = 0u; hitIndex < MAX_HITS; hitIndex += 1u) {
        outputs[query].hits[hitIndex] = emptyHit;
    }
    let directionLength = length(request.directionDistance.xyz);
    let direction = select(vec3<f32>(1.0, 0.0, 0.0),
        request.directionDistance.xyz / max(directionLength, 1e-12),
        directionLength > 1e-12);
    let maximumDistance = max(request.directionDistance.w, 0.0);
    var hitCount = 0u;
    var overflow = 0u;
    for (var body = 0u; body < params.counts.x; body += 1u) {
        if ((u32(metadata[body].w) & BODY_ALIVE) == 0u) { continue; }
        let pose = poses[body];
        let bodyPosition = body_position_in_query_frame(
            body, request.sector.xyz, u32(max(request.sector.w, 0)));
        if (bodyPosition.x > 1e30) { continue; }
        var queryPose = pose;
        queryPose.position_invMass = vec4<f32>(
            bodyPosition, pose.position_invMass.w);
        let shape = shapes[body];
        let shapeType = u32(clamp(shape.dimensions_type.w, 0.0, 4.0));
        let bodyRadius = shape_radius(shape);
        var intersection = miss();
        var metric = 0.0;
        if (queryType == QUERY_OVERLAP_SPHERE) {
            let offset = request.originRadius.xyz - bodyPosition;
            let centerDistance = length(offset);
            let combined = max(request.originRadius.w, 0.0) + bodyRadius;
            if (centerDistance <= combined) {
                let normal = normalize(select(vec3<f32>(0.0, 1.0, 0.0),
                    offset, centerDistance > 1e-8));
                metric = max(centerDistance - combined, 0.0);
                intersection = Intersection(true, metric,
                    bodyPosition + normal * bodyRadius, normal);
            }
        } else {
            var queryRadius = 0.0;
            if (queryType == QUERY_SPHERE_CAST) {
                queryRadius = max(request.originRadius.w, 0.0);
            } else if (queryType == QUERY_CAPSULE_CAST) {
                queryRadius = max(request.originRadius.w, 0.0)
                    + max(request.dimensions.w, 0.0);
            }
            if (shapeType == SHAPE_CUBE || shapeType == SHAPE_BOX) {
                intersection = ray_box(
                    request.originRadius.xyz, direction, maximumDistance,
                    queryPose, max(0.5 * abs(shape.dimensions_type.xyz)
                                  + vec3<f32>(queryRadius),
                              vec3<f32>(0.0)));
            } else {
                intersection = ray_sphere(
                    request.originRadius.xyz, direction, maximumDistance,
                    bodyPosition, bodyRadius + queryRadius);
            }
            if (maximumDistance > 1e-12) {
                metric = intersection.distance / maximumDistance;
            }
        }
        if (!intersection.hit) { continue; }
        let hit = QueryHit(
            vec4<u32>(request.ids.x, body, 0u, queryType),
            vec4<f32>(metric, intersection.distance, 0.0, 0.0),
            vec4<f32>(intersection.point, 0.0),
            vec4<f32>(intersection.normal, 0.0), request.sector);
        insert_hit(query, hit, maximumHits, &hitCount, &overflow);
    }
    outputs[query].header = vec4<u32>(
        request.ids.x, hitCount, overflow, queryType);
}
