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

const GENERATION_MASK : u32 = 0x000fffffu;
const BODY_ALIVE : u32 = 1u << 20u;
const BODY_AWAKE : u32 = 1u << 21u;
const BODY_BULLET : u32 = 1u << 22u;

const QUERY_RAY : u32 = 0u;
const QUERY_OVERLAP_SPHERE : u32 = 1u;
const QUERY_SPHERE_CAST : u32 = 2u;
const QUERY_CAPSULE_CAST : u32 = 3u;
const QUERY_EXCLUDE_STATIC : u32 = 1u << 0u;
const QUERY_EXCLUDE_DYNAMIC : u32 = 1u << 1u;
const QUERY_EXCLUDE_SLEEPING : u32 = 1u << 2u;
const QUERY_EXCLUDE_AWAKE : u32 = 1u << 3u;
const QUERY_EXCLUDE_BULLETS : u32 = 1u << 4u;

const SHAPE_SPHERE : u32 = 0u;
const SHAPE_CUBE : u32 = 1u;
const SHAPE_BOX : u32 = 2u;
const SHAPE_CAPSULE : u32 = 3u;
const SHAPE_CYLINDER : u32 = 4u;
const MAX_HITS : u32 = 16u;
const SENTINEL : u32 = 0xffffffffu;

struct BodyPose {
    position_invMass : vec4<f32>,
    orientation : vec4<f32>,
};

struct BodyShape {
    dimensions_type : vec4<f32>,
    invInertia_material : vec4<f32>,
    material_coefficients : vec4<f32>,
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

// signedDistance is negative inside the target.  normal points from the
// target toward the sample point (or toward the nearest exit when inside).
struct Surface {
    signedDistance : f32,
    point : vec3<f32>,
    normal : vec3<f32>,
    feature : u32,
};

struct Separation {
    gap : f32,
    point : vec3<f32>,
    normal : vec3<f32>,
    feature : u32,
};

struct Intersection {
    hit : bool,
    distance : f32,
    point : vec3<f32>,
    normal : vec3<f32>,
    feature : u32,
};

@group(0) @binding(0) var<storage, read> poses : array<BodyPose>;
@group(0) @binding(1) var<storage, read> shapes : array<BodyShape>;
@group(0) @binding(2) var<storage, read> metadata : array<vec4<i32>>;
@group(0) @binding(3) var<storage, read> requests : array<QueryRequest>;
@group(0) @binding(4) var<storage, read_write> outputs : array<QueryOutput>;
@group(0) @binding(5) var<uniform> params : QueryParams;

fn safe_normalize(value : vec3<f32>, fallback : vec3<f32>) -> vec3<f32> {
    let squared = dot(value, value);
    return select(fallback, value * inverseSqrt(max(squared, 1e-30)),
                  squared > 1e-20);
}

fn component(value : vec3<f32>, axis : u32) -> f32 {
    if (axis == 0u) { return value.x; }
    if (axis == 1u) { return value.y; }
    return value.z;
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

fn shape_type(shape : BodyShape) -> u32 {
    return u32(clamp(shape.dimensions_type.w, 0.0, 4.0));
}

fn sphere_radius(shape : BodyShape) -> f32 {
    return 0.5 * max(abs(shape.dimensions_type.x), 1e-5);
}

fn capsule_radius(shape : BodyShape) -> f32 {
    let dimensions = max(abs(shape.dimensions_type.xyz), vec3<f32>(1e-5));
    return 0.25 * (dimensions.x + dimensions.z);
}

fn capsule_segment(pose : BodyPose, shape : BodyShape) -> Segment {
    let radius = capsule_radius(shape);
    let halfSegment = max(
        0.5 * abs(shape.dimensions_type.y) - radius, 0.0);
    let offset = quaternion_rotate(
        pose.orientation, vec3<f32>(0.0, halfSegment, 0.0));
    return Segment(pose.position_invMass.xyz - offset,
                   pose.position_invMass.xyz + offset);
}

fn cylinder_radius(shape : BodyShape) -> f32 {
    let dimensions = max(abs(shape.dimensions_type.xyz), vec3<f32>(1e-5));
    return 0.25 * (dimensions.x + dimensions.z);
}

fn cylinder_half_height(shape : BodyShape) -> f32 {
    return 0.5 * max(abs(shape.dimensions_type.y), 1e-5);
}

fn closest_point_segment(point : vec3<f32>, segment : Segment) -> vec4<f32> {
    let edge = segment.second - segment.first;
    let denominator = dot(edge, edge);
    let fraction = select(0.0, clamp(
        dot(point - segment.first, edge) / max(denominator, 1e-30),
        0.0, 1.0), denominator > 1e-20);
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
    if (aa > 1e-20 && denominator > 1e-20) {
        fractionA = clamp((ab * bo - bb * ao) / denominator, 0.0, 1.0);
    }
    var fractionB = 0.0;
    if (bb > 1e-20) {
        fractionB = clamp((ab * fractionA + bo) / bb, 0.0, 1.0);
    }
    if (aa > 1e-20) {
        fractionA = clamp((ab * fractionB - ao) / aa, 0.0, 1.0);
    }
    return ClosestSegments(
        a.first + fractionA * directionA,
        b.first + fractionB * directionB,
        fractionA, fractionB);
}

fn feature_from_fraction(fraction : f32, base : u32) -> u32 {
    if (fraction <= 1e-4) { return base; }
    if (fraction >= 0.9999) { return base + 1u; }
    return base + 2u;
}

fn sphere_surface(pose : BodyPose, shape : BodyShape,
                  point : vec3<f32>) -> Surface {
    let delta = point - pose.position_invMass.xyz;
    let distance = length(delta);
    let normal = safe_normalize(delta, vec3<f32>(0.0, 1.0, 0.0));
    let radius = sphere_radius(shape);
    return Surface(distance - radius,
        pose.position_invMass.xyz + normal * radius, normal, 0u);
}

fn capsule_surface(pose : BodyPose, shape : BodyShape,
                   point : vec3<f32>) -> Surface {
    let closest = closest_point_segment(point, capsule_segment(pose, shape));
    let delta = point - closest.xyz;
    let normal = safe_normalize(delta, quaternion_rotate(
        pose.orientation, vec3<f32>(1.0, 0.0, 0.0)));
    let radius = capsule_radius(shape);
    return Surface(length(delta) - radius, closest.xyz + normal * radius,
        normal, feature_from_fraction(closest.w, 0x100u));
}

fn box_surface(pose : BodyPose, shape : BodyShape,
               point : vec3<f32>) -> Surface {
    let local = quaternion_inverse_rotate(
        pose.orientation, point - pose.position_invMass.xyz);
    let half = 0.5 * max(abs(shape.dimensions_type.xyz), vec3<f32>(1e-5));
    var closest = clamp(local, -half, half);
    let outside = local - closest;
    let outsideSquared = dot(outside, outside);
    if (outsideSquared > 1e-20) {
        let distance = sqrt(outsideSquared);
        let localNormal = outside / distance;
        let absoluteNormal = abs(localNormal);
        var axis = 0u;
        if (absoluteNormal.y > absoluteNormal.x) { axis = 1u; }
        if (absoluteNormal.z > component(absoluteNormal, axis)) { axis = 2u; }
        let signValue = component(localNormal, axis);
        return Surface(distance,
            pose.position_invMass.xyz
                + quaternion_rotate(pose.orientation, closest),
            quaternion_rotate(pose.orientation, localNormal),
            0x200u + axis * 2u + select(0u, 1u, signValue > 0.0));
    }

    let gaps = half - abs(local);
    var axis = 0u;
    if (gaps.y < gaps.x) { axis = 1u; }
    if (gaps.z < component(gaps, axis)) { axis = 2u; }
    let signValue = select(-1.0, 1.0, component(local, axis) >= 0.0);
    closest[axis] = signValue * half[axis];
    var localNormal = vec3<f32>(0.0);
    localNormal[axis] = signValue;
    return Surface(-component(gaps, axis),
        pose.position_invMass.xyz
            + quaternion_rotate(pose.orientation, closest),
        quaternion_rotate(pose.orientation, localNormal),
        0x200u + axis * 2u + select(0u, 1u, signValue > 0.0));
}

fn cylinder_surface(pose : BodyPose, shape : BodyShape,
                    point : vec3<f32>) -> Surface {
    let local = quaternion_inverse_rotate(
        pose.orientation, point - pose.position_invMass.xyz);
    let radius = cylinder_radius(shape);
    let halfHeight = cylinder_half_height(shape);
    let radialLength = length(local.xz);
    let radialDirection = select(vec2<f32>(1.0, 0.0),
        local.xz / max(radialLength, 1e-30), radialLength > 1e-10);
    var closest = vec3<f32>(
        radialDirection.x * min(radialLength, radius),
        clamp(local.y, -halfHeight, halfHeight),
        radialDirection.y * min(radialLength, radius));
    if (radialLength > radius || abs(local.y) > halfHeight) {
        let delta = local - closest;
        let distance = length(delta);
        let localNormal = safe_normalize(delta, vec3<f32>(1.0, 0.0, 0.0));
        let capOutside = abs(local.y) > halfHeight;
        let feature = select(0x310u,
            0x300u + select(0u, 1u, local.y > 0.0), capOutside);
        return Surface(distance,
            pose.position_invMass.xyz
                + quaternion_rotate(pose.orientation, closest),
            quaternion_rotate(pose.orientation, localNormal), feature);
    }

    let radialGap = radius - radialLength;
    let capGap = halfHeight - abs(local.y);
    var localNormal = vec3<f32>(radialDirection.x, 0.0,
                                radialDirection.y);
    var feature = 0x310u;
    var gap = radialGap;
    if (capGap < radialGap) {
        let signValue = select(-1.0, 1.0, local.y >= 0.0);
        closest.y = signValue * halfHeight;
        localNormal = vec3<f32>(0.0, signValue, 0.0);
        feature = 0x300u + select(0u, 1u, signValue > 0.0);
        gap = capGap;
    } else {
        closest.x = radialDirection.x * radius;
        closest.z = radialDirection.y * radius;
    }
    return Surface(-gap,
        pose.position_invMass.xyz
            + quaternion_rotate(pose.orientation, closest),
        quaternion_rotate(pose.orientation, localNormal), feature);
}

fn shape_surface(pose : BodyPose, shape : BodyShape,
                 point : vec3<f32>) -> Surface {
    if (legoIsBrick(bitcast<u32>(shape.invInertia_material.w))) {
        var best = Surface(1e30, point, vec3<f32>(0,1,0), 0u);
        for(var part=0u;part<legoBrickParts(shape.dimensions_type.xyz,bitcast<u32>(shape.invInertia_material.w));part++) {
            var child=shape;var childPose=pose;
            child.dimensions_type=vec4<f32>(legoBrickPartSize(shape.dimensions_type.xyz,part),select(2.0,4.0,part>0u));
            childPose.position_invMass=vec4<f32>(pose.position_invMass.xyz+quaternion_rotate(pose.orientation,legoBrickPartOffset(shape.dimensions_type.xyz,part)),pose.position_invMass.w);
            var hit=box_surface(childPose,child,point);
            if(part>0u){hit=cylinder_surface(childPose,child,point);}
            if(hit.signedDistance<best.signedDistance){best=hit;best.feature|=part<<12u;}
        }
        return best;
    }
    let kind = shape_type(shape);
    if (kind == SHAPE_SPHERE) {
        return sphere_surface(pose, shape, point);
    }
    if (kind == SHAPE_CAPSULE) {
        return capsule_surface(pose, shape, point);
    }
    if (kind == SHAPE_CYLINDER) {
        return cylinder_surface(pose, shape, point);
    }
    return box_surface(pose, shape, point);
}

fn closest_segment_surface(segment : Segment, pose : BodyPose,
                           shape : BodyShape) -> Surface {
    var bestFraction = 0.0;
    var best = shape_surface(pose, shape, segment.first);
    for (var sample = 1u; sample <= 24u; sample += 1u) {
        let fraction = f32(sample) / 24.0;
        let candidate = shape_surface(
            pose, shape, mix(segment.first, segment.second, fraction));
        if (candidate.signedDistance < best.signedDistance) {
            best = candidate;
            bestFraction = fraction;
        }
    }
    var low = max(bestFraction - 1.0 / 24.0, 0.0);
    var high = min(bestFraction + 1.0 / 24.0, 1.0);
    for (var iteration = 0u; iteration < 12u; iteration += 1u) {
        let left = mix(low, high, 0.3333333333);
        let right = mix(low, high, 0.6666666667);
        let leftSurface = shape_surface(
            pose, shape, mix(segment.first, segment.second, left));
        let rightSurface = shape_surface(
            pose, shape, mix(segment.first, segment.second, right));
        if (leftSurface.signedDistance <= rightSurface.signedDistance) {
            high = right;
        } else {
            low = left;
        }
    }
    return shape_surface(pose, shape,
        mix(segment.first, segment.second, 0.5 * (low + high)));
}

fn sphere_separation(center : vec3<f32>, radius : f32,
                     pose : BodyPose, shape : BodyShape) -> Separation {
    let surface = shape_surface(pose, shape, center);
    return Separation(surface.signedDistance - radius, surface.point,
                      surface.normal, surface.feature);
}

fn capsule_separation(center : vec3<f32>, axis : vec3<f32>,
                      halfHeight : f32, radius : f32,
                      pose : BodyPose, shape : BodyShape) -> Separation {
    let offset = safe_normalize(axis, vec3<f32>(0.0, 1.0, 0.0))
        * halfHeight;
    let querySegment = Segment(center - offset, center + offset);
    let kind = shape_type(shape);
    if (kind == SHAPE_SPHERE) {
        let closest = closest_point_segment(
            pose.position_invMass.xyz, querySegment);
        let delta = closest.xyz - pose.position_invMass.xyz;
        let normal = safe_normalize(delta, vec3<f32>(0.0, 1.0, 0.0));
        let targetRadius = sphere_radius(shape);
        return Separation(length(delta) - radius - targetRadius,
            pose.position_invMass.xyz + normal * targetRadius, normal, 0u);
    }
    if (kind == SHAPE_CAPSULE) {
        let targetSegment = capsule_segment(pose, shape);
        let closest = closest_segments(querySegment, targetSegment);
        let delta = closest.pointA - closest.pointB;
        let normal = safe_normalize(delta, vec3<f32>(0.0, 1.0, 0.0));
        let targetRadius = capsule_radius(shape);
        return Separation(length(delta) - radius - targetRadius,
            closest.pointB + normal * targetRadius, normal,
            feature_from_fraction(closest.fractionB, 0x100u));
    }
    let surface = closest_segment_surface(querySegment, pose, shape);
    return Separation(surface.signedDistance - radius, surface.point,
                      surface.normal, surface.feature);
}

fn miss() -> Intersection {
    return Intersection(false, 0.0, vec3<f32>(0.0),
                        vec3<f32>(0.0), SENTINEL);
}

fn ray_sphere(origin : vec3<f32>, direction : vec3<f32>, maximum : f32,
              center : vec3<f32>, radius : f32, feature : u32) -> Intersection {
    let offset = origin - center;
    let c = dot(offset, offset) - radius * radius;
    if (c <= 0.0) {
        let normal = safe_normalize(offset, vec3<f32>(0.0, 1.0, 0.0));
        return Intersection(true, 0.0, center + normal * radius,
                            normal, feature);
    }
    let b = dot(offset, direction);
    let discriminant = b * b - c;
    if (discriminant < 0.0) { return miss(); }
    let distance = -b - sqrt(discriminant);
    if (distance < 0.0 || distance > maximum) { return miss(); }
    let point = origin + direction * distance;
    return Intersection(true, distance, point,
                        safe_normalize(point - center,
                                       vec3<f32>(0.0, 1.0, 0.0)),
                        feature);
}

fn ray_box(origin : vec3<f32>, direction : vec3<f32>, maximum : f32,
           pose : BodyPose, shape : BodyShape) -> Intersection {
    let initialSurface = box_surface(pose, shape, origin);
    if (initialSurface.signedDistance <= 0.0) {
        return Intersection(true, 0.0, initialSurface.point,
            initialSurface.normal, initialSurface.feature);
    }
    let localOrigin = quaternion_inverse_rotate(
        pose.orientation, origin - pose.position_invMass.xyz);
    let localDirection = quaternion_inverse_rotate(pose.orientation, direction);
    let extents = 0.5 * max(abs(shape.dimensions_type.xyz), vec3<f32>(1e-5));
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
        var signValue = -1.0;
        if (first > second) {
            let temporary = first;
            first = second;
            second = temporary;
            signValue = 1.0;
        }
        if (first > near) {
            near = first;
            nearAxis = axis;
            nearSign = signValue;
        }
        far = min(far, second);
        if (near > far) { return miss(); }
    }
    if (near > maximum) { return miss(); }
    var localNormal = vec3<f32>(0.0);
    localNormal[nearAxis] = nearSign;
    let normal = quaternion_rotate(pose.orientation, localNormal);
    return Intersection(true, near, origin + direction * near, normal,
        0x200u + nearAxis * 2u + select(0u, 1u, nearSign > 0.0));
}

fn nearer(first : Intersection, second : Intersection) -> Intersection {
    if (!first.hit) { return second; }
    if (!second.hit) { return first; }
    if (first.distance <= second.distance) { return first; }
    return second;
}

fn ray_capsule(origin : vec3<f32>, direction : vec3<f32>, maximum : f32,
               pose : BodyPose, shape : BodyShape) -> Intersection {
    let initialSurface = capsule_surface(pose, shape, origin);
    if (initialSurface.signedDistance <= 0.0) {
        return Intersection(true, 0.0, initialSurface.point,
            initialSurface.normal, initialSurface.feature);
    }
    let segment = capsule_segment(pose, shape);
    let radius = capsule_radius(shape);
    let axis = segment.second - segment.first;
    let offset = origin - segment.first;
    let axisSquared = dot(axis, axis);
    let axisRay = dot(axis, direction);
    let axisOffset = dot(axis, offset);
    let rayOffset = dot(direction, offset);
    let offsetSquared = dot(offset, offset);
    let coefficientA = axisSquared - axisRay * axisRay;
    let coefficientB = axisSquared * rayOffset - axisOffset * axisRay;
    let coefficientC = axisSquared * offsetSquared
        - axisOffset * axisOffset - radius * radius * axisSquared;
    var result = miss();
    let discriminant = coefficientB * coefficientB
        - coefficientA * coefficientC;
    if (abs(coefficientA) > 1e-20 && discriminant >= 0.0) {
        let distance = (-coefficientB - sqrt(discriminant)) / coefficientA;
        let height = axisOffset + distance * axisRay;
        if (distance >= 0.0 && distance <= maximum
            && height > 0.0 && height < axisSquared) {
            let point = origin + direction * distance;
            let axisPoint = segment.first + axis * (height / axisSquared);
            result = Intersection(true, distance, point,
                safe_normalize(point - axisPoint,
                               vec3<f32>(1.0, 0.0, 0.0)), 0x102u);
        }
    }
    result = nearer(result, ray_sphere(origin, direction, maximum,
        segment.first, radius, 0x100u));
    result = nearer(result, ray_sphere(origin, direction, maximum,
        segment.second, radius, 0x101u));
    return result;
}

fn ray_cylinder(origin : vec3<f32>, direction : vec3<f32>, maximum : f32,
                pose : BodyPose, shape : BodyShape) -> Intersection {
    let initialSurface = cylinder_surface(pose, shape, origin);
    if (initialSurface.signedDistance <= 0.0) {
        return Intersection(true, 0.0, initialSurface.point,
            initialSurface.normal, initialSurface.feature);
    }
    let localOrigin = quaternion_inverse_rotate(
        pose.orientation, origin - pose.position_invMass.xyz);
    let localDirection = quaternion_inverse_rotate(pose.orientation, direction);
    let radius = cylinder_radius(shape);
    let halfHeight = cylinder_half_height(shape);
    var result = miss();

    let coefficientA = dot(localDirection.xz, localDirection.xz);
    let coefficientB = dot(localOrigin.xz, localDirection.xz);
    let coefficientC = dot(localOrigin.xz, localOrigin.xz) - radius * radius;
    let discriminant = coefficientB * coefficientB
        - coefficientA * coefficientC;
    if (coefficientA > 1e-20 && discriminant >= 0.0) {
        let distance = (-coefficientB - sqrt(discriminant)) / coefficientA;
        let height = localOrigin.y + localDirection.y * distance;
        if (distance >= 0.0 && distance <= maximum
            && abs(height) <= halfHeight) {
            let localPoint = localOrigin + localDirection * distance;
            let localNormal = safe_normalize(
                vec3<f32>(localPoint.x, 0.0, localPoint.z),
                vec3<f32>(1.0, 0.0, 0.0));
            result = Intersection(true, distance,
                origin + direction * distance,
                quaternion_rotate(pose.orientation, localNormal), 0x310u);
        }
    }
    if (abs(localDirection.y) > 1e-20) {
        for (var cap = 0u; cap < 2u; cap += 1u) {
            let signValue = select(-1.0, 1.0, cap == 1u);
            let distance = (signValue * halfHeight - localOrigin.y)
                / localDirection.y;
            let localPoint = localOrigin + localDirection * distance;
            if (distance >= 0.0 && distance <= maximum
                && dot(localPoint.xz, localPoint.xz) <= radius * radius) {
                let candidate = Intersection(true, distance,
                    origin + direction * distance,
                    quaternion_rotate(pose.orientation,
                        vec3<f32>(0.0, signValue, 0.0)),
                    0x300u + cap);
                result = nearer(result, candidate);
            }
        }
    }
    return result;
}

fn ray_shape(origin : vec3<f32>, direction : vec3<f32>, maximum : f32,
             pose : BodyPose, shape : BodyShape) -> Intersection {
    if (legoIsBrick(bitcast<u32>(shape.invInertia_material.w))) {
        var best=miss();
        for(var part=0u;part<legoBrickParts(shape.dimensions_type.xyz,bitcast<u32>(shape.invInertia_material.w));part++) {
            var child=shape;var childPose=pose;
            child.dimensions_type=vec4<f32>(legoBrickPartSize(shape.dimensions_type.xyz,part),select(2.0,4.0,part>0u));
            childPose.position_invMass=vec4<f32>(pose.position_invMass.xyz+quaternion_rotate(pose.orientation,legoBrickPartOffset(shape.dimensions_type.xyz,part)),pose.position_invMass.w);
            var hit=ray_box(origin,direction,maximum,childPose,child);
            if(part>0u){hit=ray_cylinder(origin,direction,maximum,childPose,child);}
            hit.feature|=part<<12u;best=nearer(best,hit);
        }
        return best;
    }
    let kind = shape_type(shape);
    if (kind == SHAPE_SPHERE) {
        return ray_sphere(origin, direction, maximum,
            pose.position_invMass.xyz, sphere_radius(shape), 0u);
    }
    if (kind == SHAPE_CAPSULE) {
        return ray_capsule(origin, direction, maximum, pose, shape);
    }
    if (kind == SHAPE_CYLINDER) {
        return ray_cylinder(origin, direction, maximum, pose, shape);
    }
    return ray_box(origin, direction, maximum, pose, shape);
}

fn sphere_cast(origin : vec3<f32>, radius : f32,
               direction : vec3<f32>, maximum : f32,
               pose : BodyPose, shape : BodyShape) -> Intersection {
    var distance = 0.0;
    for (var iteration = 0u; iteration < 64u; iteration += 1u) {
        let center = origin + direction * distance;
        let separation = sphere_separation(center, radius, pose, shape);
        if (separation.gap <= 1e-4) {
            return Intersection(true, distance, separation.point,
                separation.normal, separation.feature);
        }
        if (distance >= maximum) { return miss(); }
        distance = min(distance + max(separation.gap, 1e-5), maximum);
    }
    return miss();
}

fn capsule_cast(origin : vec3<f32>, radius : f32,
                axis : vec3<f32>, halfHeight : f32,
                direction : vec3<f32>, maximum : f32,
                pose : BodyPose, shape : BodyShape) -> Intersection {
    var distance = 0.0;
    for (var iteration = 0u; iteration < 96u; iteration += 1u) {
        let center = origin + direction * distance;
        let separation = capsule_separation(
            center, axis, halfHeight, radius, pose, shape);
        if (separation.gap <= 1e-4) {
            return Intersection(true, distance, separation.point,
                separation.normal, separation.feature);
        }
        if (distance >= maximum) { return miss(); }
        // The sampled segment distance is deliberately advanced
        // conservatively so a narrow edge/cap cannot be skipped.
        distance = min(distance + max(0.8 * separation.gap, 1e-5), maximum);
    }
    return miss();
}

fn body_is_filtered(flags : u32, pose : BodyPose, packedMetadata : u32) -> bool {
    let isStatic = pose.position_invMass.w <= 0.0;
    let isAwake = (packedMetadata & BODY_AWAKE) != 0u;
    let isBullet = (packedMetadata & BODY_BULLET) != 0u;
    return (isStatic && (flags & QUERY_EXCLUDE_STATIC) != 0u)
        || (!isStatic && (flags & QUERY_EXCLUDE_DYNAMIC) != 0u)
        || (!isAwake && (flags & QUERY_EXCLUDE_SLEEPING) != 0u)
        || (isAwake && (flags & QUERY_EXCLUDE_AWAKE) != 0u)
        || (isBullet && (flags & QUERY_EXCLUDE_BULLETS) != 0u);
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
        vec4<i32>(request.sector.xyz, 0));
    for (var hitIndex = 0u; hitIndex < MAX_HITS; hitIndex += 1u) {
        outputs[query].hits[hitIndex] = emptyHit;
    }
    let directionLength = length(request.directionDistance.xyz);
    let direction = request.directionDistance.xyz
        / max(directionLength, 1e-20);
    let maximumDistance = max(request.directionDistance.w, 0.0);
    let queryRadius = max(request.originRadius.w, 0.0);
    let capsuleAxis = request.dimensions.xyz
        / max(length(request.dimensions.xyz), 1e-20);
    let capsuleHalfHeight = max(request.dimensions.w, 0.0);
    var hitCount = 0u;
    var overflow = 0u;
    for (var body = 0u; body < params.counts.x; body += 1u) {
        let packedMetadata = u32(metadata[body].w);
        if ((packedMetadata & BODY_ALIVE) == 0u) { continue; }
        let sourcePose = poses[body];
        if (body_is_filtered(request.ids.w, sourcePose, packedMetadata)) {
            continue;
        }
        let bodyPosition = body_position_in_query_frame(
            body, request.sector.xyz, u32(max(request.sector.w, 0)));
        if (bodyPosition.x > 1e30) { continue; }
        var pose = sourcePose;
        pose.position_invMass = vec4<f32>(
            bodyPosition, sourcePose.position_invMass.w);
        let shape = shapes[body];
        var intersection = miss();
        var metric = 0.0;
        if (queryType == QUERY_OVERLAP_SPHERE) {
            let separation = sphere_separation(
                request.originRadius.xyz, queryRadius, pose, shape);
            if (separation.gap <= 0.0) {
                intersection = Intersection(true, max(-separation.gap, 0.0),
                    separation.point, separation.normal, separation.feature);
            }
        } else if (queryType == QUERY_RAY) {
            if (directionLength > 1e-12) {
                intersection = ray_shape(request.originRadius.xyz,
                    direction, maximumDistance, pose, shape);
            }
        } else if (queryType == QUERY_SPHERE_CAST) {
            if (directionLength > 1e-12) {
                intersection = sphere_cast(request.originRadius.xyz,
                    queryRadius, direction, maximumDistance, pose, shape);
            }
        } else if (directionLength > 1e-12
                   && length(request.dimensions.xyz) > 1e-12) {
            intersection = capsule_cast(request.originRadius.xyz,
                queryRadius, capsuleAxis, capsuleHalfHeight,
                direction, maximumDistance, pose, shape);
        }
        if (!intersection.hit) { continue; }
        if (queryType != QUERY_OVERLAP_SPHERE
            && maximumDistance > 1e-12) {
            metric = intersection.distance / maximumDistance;
        }
        let hit = QueryHit(
            vec4<u32>(request.ids.x, body, intersection.feature, queryType),
            vec4<f32>(metric, intersection.distance, 0.0, 0.0),
            vec4<f32>(intersection.point, 0.0),
            vec4<f32>(intersection.normal, 0.0),
            vec4<i32>(request.sector.xyz,
                i32(packedMetadata & GENERATION_MASK)));
        insert_hit(query, hit, maximumHits, &hitCount, &overflow);
    }
    outputs[query].header = vec4<u32>(
        request.ids.x, hitCount, overflow, queryType);
}
