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
    let shape = shapes[body];
    let shapeType = u32(clamp(shape.dimensions_type.w, 0.0, 4.0));
    if (!bullet && shapeType != SHAPE_SPHERE
        && shapeType != SHAPE_CAPSULE) { return; }
    var pose = poses[body];
    var motion = motions[body];
    let translation = motion.linearVelocity_sleep.xyz * ccd.tuning.x;
    let distance = length(translation);
    let radius = shape_bounding_radius(shape);
    if (!bullet && distance <= ccd.tuning.y * radius) { return; }
    if (!bullet) { atomicAdd(&telemetry[0], 1u); }
    if (distance <= 1e-8) { return; }
    var terrainFrameValid = false;
    let terrainPose = terrain_frame_pose(
        pose, metadata[body], radius, &terrainFrameValid);
    if (!terrainFrameValid) { return; }
    let underResolved = distance > f32(ccd.counts.z)
                     * ccd.terrainOrigin_cell_height.z;
    if (underResolved) {
        atomicAdd(&telemetry[5], 1u);
    }
    let sweep = sweep_terrain(terrainPose, shape, translation);
    atomicMax(&telemetry[7], sweep.iterations);
    if (!sweep.hit) {
        if (bullet && underResolved) {
            metadata[body].w = bitcast<i32>(
                u32(metadata[body].w) | BODY_CCD_FAILURE);
            atomicAdd(&telemetry[6], 1u);
        }
        return;
    }
    let safeFraction = max(
        sweep.fraction - ccd.tuning.z / max(distance, 1e-8), 0.0);
    pose.position_invMass = vec4<f32>(
        pose.position_invMass.xyz + translation * safeFraction,
        pose.position_invMass.w);
    let velocity = motion.linearVelocity_sleep.xyz;
    let inward = min(dot(velocity, sweep.normal), 0.0);
    let tangent = velocity - sweep.normal * inward;
    motion.linearVelocity_sleep = vec4<f32>(
        tangent * (1.0 - safeFraction), motion.linearVelocity_sleep.w);
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
    let bulletCount = min(compactResult[0], ccd.counts.y);
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
