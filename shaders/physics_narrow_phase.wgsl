const SHAPE_SPHERE : u32 = 0u;
const SHAPE_CUBE : u32 = 1u;
const SHAPE_BOX : u32 = 2u;
const SHAPE_CAPSULE : u32 = 3u;
const SHAPE_CYLINDER : u32 = 4u;
const PAIR_CLASS_COUNT : u32 = 10u;
const MAX_CANDIDATES : u32 = 16u;
const SENTINEL : u32 = 0xffffffffu;

struct BodyPose {
    position_invMass : vec4<f32>,
    orientation : vec4<f32>,
};

struct BodyShape {
    dimensions_type : vec4<f32>,
    invInertia_material : vec4<f32>,
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

fn pair_class_for_record(pair : KeyValue) -> u32 {
    let shapeA = canonical_shape(shapes[pair.keyHigh].dimensions_type.w);
    let shapeB = canonical_shape(shapes[pair.keyLow].dimensions_type.w);
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
    var sectorDelta = vec3<i32>(0);
    for (var axis = 0u; axis < 3u; axis += 1u) {
        sectorDelta[axis] = adjacent_sector_delta(
            metadata[frameBody][axis], metadata[body][axis]);
        if (abs(sectorDelta[axis]) > 1) {
            return vec3<f32>(3.402823466e+38);
        }
    }
    return poses[body].position_invMass.xyz
         + vec3<f32>(sectorDelta) * 256.0;
}

fn make_box(body : u32, frameBody : u32) -> BoxFrame {
    let pose = poses[body];
    var result : BoxFrame;
    result.center = body_position_in_frame(body, frameBody);
    result.axisX = quaternion_rotate(
        pose.orientation, vec3<f32>(1.0, 0.0, 0.0));
    result.axisY = quaternion_rotate(
        pose.orientation, vec3<f32>(0.0, 1.0, 0.0));
    result.axisZ = quaternion_rotate(
        pose.orientation, vec3<f32>(0.0, 0.0, 1.0));
    result.half = 0.5 * max(abs(shapes[body].dimensions_type.xyz),
                            vec3<f32>(1e-5));
    return result;
}

fn sphere_radius(body : u32) -> f32 {
    return 0.5 * max(abs(shapes[body].dimensions_type.x), 1e-5);
}

fn capsule_radius(body : u32) -> f32 {
    let dimensions = max(abs(shapes[body].dimensions_type.xyz),
                         vec3<f32>(1e-5));
    return 0.25 * (dimensions.x + dimensions.z);
}

fn capsule_segment(body : u32, frameBody : u32) -> Segment {
    let radius = capsule_radius(body);
    let halfSegment = max(0.5 * abs(shapes[body].dimensions_type.y)
                              - radius, 0.0);
    let offset = quaternion_rotate(
        poses[body].orientation, vec3<f32>(0.0, halfSegment, 0.0));
    let center = body_position_in_frame(body, frameBody);
    return Segment(center - offset, center + offset);
}

fn cylinder_radius(body : u32) -> f32 {
    let dimensions = max(abs(shapes[body].dimensions_type.xyz),
                         vec3<f32>(1e-5));
    return 0.25 * (dimensions.x + dimensions.z);
}

fn cylinder_half_height(body : u32) -> f32 {
    return 0.5 * max(abs(shapes[body].dimensions_type.y), 1e-5);
}

fn shape_bounding_radius(body : u32) -> f32 {
    let dimensions = max(abs(shapes[body].dimensions_type.xyz),
                         vec3<f32>(1e-5));
    let shapeClass = canonical_shape(shapes[body].dimensions_type.w);
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

fn bounding_spheres_may_touch(bodyA : u32, bodyB : u32) -> bool {
    let delta = body_position_in_frame(bodyB, bodyA)
              - body_position_in_frame(bodyA, bodyA);
    let reach = shape_bounding_radius(bodyA)
              + shape_bounding_radius(bodyB) + narrow.tolerances.y;
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
        || separation > narrow.tolerances.y
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
    let pose = poses[body];
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
    if (separation > narrow.tolerances.y) {
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
        let signU = select(-1.0, 1.0, (corner & 1u) != 0u);
        let signV = select(-1.0, 1.0, (corner & 2u) != 0u);
        polygon.points[corner].position = incidentCenter
            + signU * incidentU + signV * incidentV;
        polygon.points[corner].feature = 0x240u + incidentAxis * 8u
            + select(0u, 4u, incidentSign > 0.0) + corner;
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
    let frameA = make_box(bodyA, frameBody);
    let frameB = make_box(bodyB, frameBody);
    let sat = box_box_sat(frameA, frameB);
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

fn cylinder_vertex(body : u32, index : u32,
                   frameBody : u32) -> vec3<f32> {
    let ring = index & 7u;
    let angle = 0.7853981633974483 * f32(ring);
    let local = vec3<f32>(cos(angle) * cylinder_radius(body),
        select(-cylinder_half_height(body), cylinder_half_height(body),
               index >= 8u),
        sin(angle) * cylinder_radius(body));
    return body_position_in_frame(body, frameBody)
        + quaternion_rotate(poses[body].orientation, local);
}

fn poly_vertex_count(shapeCategory : u32) -> u32 {
    return select(8u, 16u, shapeCategory == 3u);
}

fn poly_vertex(body : u32, shapeCategory : u32, index : u32,
               frameBody : u32) -> vec3<f32> {
    if (shapeCategory == 3u) {
        return cylinder_vertex(body, index, frameBody);
    }
    return box_vertex(make_box(body, frameBody), index);
}

fn poly_face_axis_count(shapeCategory : u32) -> u32 {
    return select(3u, 9u, shapeCategory == 3u);
}

fn poly_face_axis(body : u32, shapeCategory : u32,
                  index : u32, frameBody : u32) -> vec3<f32> {
    if (shapeCategory != 3u) {
        return axis_value(make_box(body, frameBody), index);
    }
    if (index == 0u) {
        return quaternion_rotate(
            poses[body].orientation, vec3<f32>(0.0, 1.0, 0.0));
    }
    let angle = 0.7853981633974483 * (f32(index - 1u) + 0.5);
    return quaternion_rotate(poses[body].orientation,
        vec3<f32>(cos(angle), 0.0, sin(angle)));
}

fn poly_edge_axis_count(shapeCategory : u32) -> u32 {
    return select(3u, 9u, shapeCategory == 3u);
}

fn poly_edge_axis(body : u32, shapeCategory : u32,
                  index : u32, frameBody : u32) -> vec3<f32> {
    if (shapeCategory != 3u) {
        return axis_value(make_box(body, frameBody), index);
    }
    if (index == 0u) {
        return quaternion_rotate(
            poses[body].orientation, vec3<f32>(0.0, 1.0, 0.0));
    }
    let angle = 0.7853981633974483 * (f32(index - 1u) + 0.5);
    return quaternion_rotate(poses[body].orientation,
        vec3<f32>(-sin(angle), 0.0, cos(angle)));
}

fn projected_poly_range(body : u32, shapeCategory : u32,
                        axis : vec3<f32>, frameBody : u32) -> vec2<f32> {
    let count = poly_vertex_count(shapeCategory);
    var minimum = dot(poly_vertex(
        body, shapeCategory, 0u, frameBody), axis);
    var maximum = minimum;
    for (var index = 1u; index < count; index += 1u) {
        let projection = dot(poly_vertex(
            body, shapeCategory, index, frameBody), axis);
        minimum = min(minimum, projection);
        maximum = max(maximum, projection);
    }
    return vec2<f32>(minimum, maximum);
}

fn consider_poly_sat_axis(sat : ptr<function, SatResult>,
                          rawAxis : vec3<f32>, kind : u32,
                          axisA : u32, axisB : u32,
                          bodyA : u32, categoryA : u32,
                          bodyB : u32, categoryB : u32,
                          frameBody : u32) {
    let squared = dot(rawAxis, rawAxis);
    if (squared <= 1e-10) { return; }
    var axis = rawAxis * inverseSqrt(squared);
    if (dot(body_position_in_frame(bodyB, frameBody)
          - body_position_in_frame(bodyA, frameBody), axis) < 0.0) {
        axis = -axis;
    }
    let rangeA = projected_poly_range(bodyA, categoryA, axis, frameBody);
    let rangeB = projected_poly_range(bodyB, categoryB, axis, frameBody);
    let separation = rangeB.x - rangeA.y;
    if (separation > narrow.tolerances.y) { (*sat).valid = 0u; }
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

fn polyhedron_sat(bodyA : u32, categoryA : u32,
                  bodyB : u32, categoryB : u32,
                  frameBody : u32) -> SatResult {
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
            poly_face_axis(bodyA, categoryA, axis, frameBody),
            0u, axis, 0u, bodyA, categoryA, bodyB, categoryB, frameBody);
        if (result.valid == 0u) { return result; }
    }
    for (var axis = 0u; axis < poly_face_axis_count(categoryB);
         axis += 1u) {
        consider_poly_sat_axis(&result,
            poly_face_axis(bodyB, categoryB, axis, frameBody),
            1u, 0u, axis, bodyA, categoryA, bodyB, categoryB, frameBody);
        if (result.valid == 0u) { return result; }
    }
    for (var axisA = 0u; axisA < poly_edge_axis_count(categoryA);
         axisA += 1u) {
        for (var axisB = 0u; axisB < poly_edge_axis_count(categoryB);
             axisB += 1u) {
            consider_poly_sat_axis(&result, cross(
                poly_edge_axis(bodyA, categoryA, axisA, frameBody),
                poly_edge_axis(bodyB, categoryB, axisB, frameBody)),
                2u, axisA, axisB,
                bodyA, categoryA, bodyB, categoryB, frameBody);
            if (result.valid == 0u) { return result; }
        }
    }
    return result;
}

fn collide_polyhedra(bodyA : u32, categoryA : u32,
                     bodyB : u32, categoryB : u32,
                     frameBody : u32) -> CandidateSet {
    var result = empty_candidates();
    let sat = polyhedron_sat(
        bodyA, categoryA, bodyB, categoryB, frameBody);
    result.normal = sat.normal;
    if (sat.valid == 0u) { return result; }
    let rangeA = projected_poly_range(
        bodyA, categoryA, sat.normal, frameBody);
    let rangeB = projected_poly_range(
        bodyB, categoryB, sat.normal, frameBody);
    let separation = rangeB.x - rangeA.y;
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
        let vertex = poly_vertex(bodyA, categoryA, index, frameBody);
        if (dot(vertex, sat.normal) >= rangeA.y - supportTolerance) {
            if (supportA == 0u) { firstA = index; }
            averageA += vertex;
            supportA += 1u;
        }
    }
    for (var index = 0u; index < countB; index += 1u) {
        let vertex = poly_vertex(bodyB, categoryB, index, frameBody);
        if (dot(vertex, sat.normal) <= rangeB.x + supportTolerance) {
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
    let primaryA = transverse + sat.normal * rangeA.y;
    let primaryB = transverse + sat.normal * rangeB.x;
    append_candidate(&result, primaryA, primaryB, separation,
                     0x400u + firstA, 0x400u + firstB);
    for (var index = 0u; index < countA && result.count < MAX_CANDIDATES;
         index += 1u) {
        let vertex = poly_vertex(bodyA, categoryA, index, frameBody);
        if (dot(vertex, sat.normal) >= rangeA.y - supportTolerance) {
            append_candidate(&result, vertex,
                vertex + sat.normal * separation, separation,
                0x400u + index, 0x480u + firstB);
        }
    }
    for (var index = 0u; index < countB && result.count < MAX_CANDIDATES;
         index += 1u) {
        let vertex = poly_vertex(bodyB, categoryB, index, frameBody);
        if (dot(vertex, sat.normal) <= rangeB.x + supportTolerance) {
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

fn reduce_candidates(sourceInput : CandidateSet) -> CandidateSet {
    var source = sourceInput;
    var result = empty_candidates();
    result.normal = source.normal;
    if (source.count == 0u) { return result; }
    for (var index = 1u; index < source.count; index += 1u) {
        let item = source.items[index];
        var insertion = index;
        loop {
            if (insertion == 0u
                || !candidate_precedes(item, source.items[insertion - 1u])) {
                break;
            }
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
            if (minimumSpread > bestSpread + 1e-9
                || (abs(minimumSpread - bestSpread) <= 1e-9
                    && (best == SENTINEL
                        || candidate_precedes(source.items[candidate],
                                              source.items[best])))) {
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
        let localA = quaternion_inverse_rotate(poses[bodyA].orientation,
            candidate.pointA_separation.xyz
                - body_position_in_frame(bodyA, bodyA));
        let localB = quaternion_inverse_rotate(poses[bodyB].orientation,
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
              / max(narrow.tolerances.y, 1e-6), 0.05, 1.0);
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
        poses[bodyA].orientation,
        center - body_position_in_frame(bodyA, bodyA)), 0.0);
    result.frictionAnchorB = vec4<f32>(quaternion_inverse_rotate(
        poses[bodyB].orientation,
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
    if (localIndex >= atomicLoad(&classTable[pairClass])) {
        return KeyValue(0u, 0u, 0u, SENTINEL);
    }
    let bucketIndex = atomicLoad(
        &classTable[PAIR_CLASS_COUNT + pairClass]) + localIndex;
    return bucketedPairs[bucketIndex];
}

fn write_class_manifold(pairRecord : KeyValue, candidates : CandidateSet) {
    if (pairRecord.ordinal >= narrow.capacities.z) { return; }
    currentManifolds[pairRecord.ordinal] = build_manifold(
        pairRecord, candidates);
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
    if (canonical_shape(shapes[bodyA].dimensions_type.w) == 0u) {
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
    let bodyA = pairRecord.keyHigh;
    let bodyB = pairRecord.keyLow;
    if (canonical_shape(shapes[bodyA].dimensions_type.w) == 0u) {
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
    let bodyA = pairRecord.keyHigh;
    let bodyB = pairRecord.keyLow;
    if (canonical_shape(shapes[bodyA].dimensions_type.w) == 1u) {
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
    write_class_manifold(pairRecord, collide_box_box(
        pairRecord.keyHigh, pairRecord.keyLow, pairRecord.keyHigh));
}

fn narrow_sphere_cylinder_impl(gid : vec3<u32>) {
    let pairRecord = class_pair_record(gid, 6u);
    if (pairRecord.ordinal >= narrow.capacities.z) { return; }
    let bodyA = pairRecord.keyHigh;
    let bodyB = pairRecord.keyLow;
    if (canonical_shape(shapes[bodyA].dimensions_type.w) == 0u) {
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
    if (canonical_shape(shapes[bodyA].dimensions_type.w) == 1u) {
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
    let bodyA = pairRecord.keyHigh;
    let bodyB = pairRecord.keyLow;
    write_class_manifold(pairRecord, collide_polyhedra(
        bodyA,
        canonical_shape(shapes[bodyA].dimensions_type.w),
        bodyB,
        canonical_shape(shapes[bodyB].dimensions_type.w),
        bodyA));
}

fn narrow_cylinder_cylinder_impl(gid : vec3<u32>) {
    let pairRecord = class_pair_record(gid, 9u);
    if (pairRecord.ordinal >= narrow.capacities.z) { return; }
    let bodyA = pairRecord.keyHigh;
    let bodyB = pairRecord.keyLow;
    write_class_manifold(pairRecord, collide_polyhedra(
        bodyA,
        canonical_shape(shapes[bodyA].dimensions_type.w),
        bodyB,
        canonical_shape(shapes[bodyB].dimensions_type.w),
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
