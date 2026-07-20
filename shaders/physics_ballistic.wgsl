const GENERATION_MASK : u32 = 0x000fffffu;
const BODY_ALIVE : u32 = 1u << 20u;
const BODY_AWAKE : u32 = 1u << 21u;
const BODY_BULLET : u32 = 1u << 22u;
const BODY_CCD_HIT : u32 = 1u << 23u;
const BODY_CCD_FAILURE : u32 = 1u << 24u;
const BODY_KINEMATIC : u32 = 1u << 31u;
const WORKGROUP_SIZE : u32 = 256u;
const SHAPE_SPHERE : u32 = 0u;
const SHAPE_CUBE : u32 = 1u;
const SHAPE_BOX : u32 = 2u;
const SHAPE_CAPSULE : u32 = 3u;
const SHAPE_CYLINDER : u32 = 4u;
// The integer part of dimensions_type.w remains the public shape type. The
// fractional part carries a conservative one-tick linear-motion bound to broad and
// narrow phase without adding another storage-buffer binding. 256 m is well
// above the configured 500 m/s linear clamp at 60 Hz; 0.999 keeps every
// packed type below the next integer after f32 rounding.
const SHAPE_SWEEP_RANGE : f32 = 256.0;
const SHAPE_SWEEP_MAX_FRACTION : f32 = 0.999;
const MAX_TERRAIN_CANDIDATES : u32 = 16u;
const MAX_TERRAIN_CONTACTS : u32 = 4u;
const TERRAIN_MIP_REJECTED : u32 = 1u << 25u;
const BODY_SUBMERGED : u32 = 1u << 26u;
const TERRAIN_CONTACT_SHIFT : u32 = 27u;
const TERRAIN_CONTACT_MASK : u32 = 0xfu << TERRAIN_CONTACT_SHIFT;
const TERRAIN_ROLLING_RESISTANCE : f32 = 0.01;
const WORLD_SECTOR_SIZE : f32 = 256.0;
const WORLD_SECTOR_HALF : f32 = 128.0;

const COMMAND_SPAWN : u32 = 0u;
const COMMAND_DESTROY : u32 = 1u;
const COMMAND_IMPULSE : u32 = 2u;
const COMMAND_FORCE : u32 = 3u;
const COMMAND_SET_VELOCITY : u32 = 4u;
const COMMAND_SET_ANGULAR_VELOCITY : u32 = 5u;
const COMMAND_TELEPORT : u32 = 6u;
const COMMAND_SET_MATERIAL : u32 = 7u;
const COMMAND_WAKE : u32 = 8u;
const COMMAND_SLEEP : u32 = 9u;
const COMMAND_KINEMATIC_TARGET : u32 = 10u;

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

struct GpuCommand {
    header : vec4<u32>,
    p0 : vec4<f32>,
    p1 : vec4<f32>,
    p2 : vec4<f32>,
    p3 : vec4<f32>,
    p4 : vec4<f32>,
    p5 : vec4<i32>,
    p6 : vec4<f32>,
};

struct SimulationUniforms {
    gravity_dt : vec4<f32>,
    damping_clamps : vec4<f32>,
    counts : vec4<u32>,
    debugRange : vec4<u32>,
    terrainOrigin_cell_height : vec4<f32>,
    terrainSize_mips_flags : vec4<u32>,
    water : vec4<f32>,
    contact : vec4<f32>,
    solver : vec4<f32>,
    terrainMaterials : vec4<f32>,
    bodyMaterials : vec4<f32>,
    waterSurface : vec4<f32>,
    worldSector : vec4<i32>,
};

struct TerrainContactCache {
    normalImpulses : vec4<f32>,
    featureIds : vec4<u32>,
    state : vec4<u32>,
};

@group(0) @binding(0) var<storage, read_write> poses : array<BodyPose>;
@group(0) @binding(1) var<storage, read_write> motions : array<BodyMotion>;
@group(0) @binding(2) var<storage, read_write> shapes : array<BodyShape>;
// xyz are signed world sectors. w is the full body generation.
@group(0) @binding(3) var<storage, read_write> metadata : array<vec4<i32>>;
@group(0) @binding(5) var<storage, read_write> forces : array<vec4<f32>>;
// Core telemetry layout (all words are u32 atomics):
//  0 active, 1 tick, 2 terrain bodies, 3 terrain points,
//  4 max terrain points/body, 5 submerged bodies, 6 water samples,
//  7 applied commands, 8..13 matching high-water marks,
// 14 active-list overflow, 15 resident kinematic bodies.
@group(0) @binding(6) var<storage, read_write> counters : array<atomic<u32>>;
@group(0) @binding(7) var<storage, read> commands : array<GpuCommand>;
@group(0) @binding(8) var<uniform> sim : SimulationUniforms;
@group(0) @binding(9) var<storage, read_write> activeBodyIds : array<u32>;
@group(0) @binding(10) var<storage, read_write> activeOffsets : array<u32>;
@group(0) @binding(11) var<storage, read_write> blockSums : array<u32>;
@group(0) @binding(12) var<storage, read_write> blockPrefix : array<u32>;
@group(0) @binding(13) var<storage, read_write> debugPacked : array<vec4<u32>>;
@group(0) @binding(14) var maxHeightTexture : texture_2d<u32>;
@group(0) @binding(15) var<storage, read_write> terrainContactCaches :
    array<TerrainContactCache>;
@group(0) @binding(16) var waterDisplacementTexture :
    texture_2d_array<f32>;
@group(0) @binding(17) var waterDisplacementSampler : sampler;
fn terrain_friction(body : u32) -> f32 {
    let ratio = max(shapes[body].material_coefficients.x, 0.0)
        / max(sim.bodyMaterials.x, 1e-7);
    return sim.contact.y * sqrt(ratio);
}

fn terrain_restitution(body : u32, shapeType : u32) -> f32 {
    let terrainDefault = select(
        sim.terrainMaterials.z, sim.terrainMaterials.y,
        shapeType == SHAPE_SPHERE);
    let bodyDefault = select(
        sim.bodyMaterials.z, sim.bodyMaterials.y,
        shapeType == SHAPE_SPHERE);
    return terrainDefault * shapes[body].material_coefficients.y
        / max(bodyDefault, 1e-7);
}

fn terrain_rolling_resistance(body : u32) -> f32 {
    return max(shapes[body].material_coefficients.z, 0.0);
}

fn body_buoyancy(shapeType : u32) -> f32 {
    if (shapeType == SHAPE_SPHERE) { return sim.water.z; }
    if (shapeType == SHAPE_CUBE) { return 0.94; }
    if (shapeType == SHAPE_BOX) { return 1.18; }
    if (shapeType == SHAPE_CAPSULE) { return 1.10; }
    return 0.98;
}

fn is_live(index : u32, generation : u32) -> bool {
    return index > 0u && index < sim.counts.x
        && metadata_generation(index) == (generation & GENERATION_MASK)
        && (body_flags(index) & BODY_ALIVE) != 0u;
}

fn body_flags(body : u32) -> u32 {
    return u32(metadata[body].w) & ~GENERATION_MASK;
}

fn set_body_flags(body : u32, flags : u32) {
    metadata[body].w = bitcast<i32>(metadata_generation(body)
        | (flags & ~GENERATION_MASK));
}

fn metadata_generation(body : u32) -> u32 {
    return u32(metadata[body].w) & GENERATION_MASK;
}

fn pack_generation_flags(generation : u32, flags : u32) -> i32 {
    return bitcast<i32>((generation & GENERATION_MASK)
        | (flags & ~GENERATION_MASK));
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
        let step = i32(floor(
            (local + WORLD_SECTOR_HALF) / WORLD_SECTOR_SIZE));
        if (step == 0) { continue; }
        let oldSector = (*worldMeta)[axis];
        let newSector = saturating_sector_step(oldSector, step);
        if (newSector == oldSector) {
            (*pose).position_invMass[axis] = clamp(
                local, -WORLD_SECTOR_HALF,
                bitcast<f32>(bitcast<u32>(WORLD_SECTOR_HALF) - 1u));
        } else {
            (*worldMeta)[axis] = newSector;
            (*pose).position_invMass[axis] =
                local - f32(step) * WORLD_SECTOR_SIZE;
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

fn pose_in_world_frame(pose : BodyPose, worldMeta : vec4<i32>,
                       maximumHorizontalSectors : u32,
                       maximumVerticalSectors : u32,
                       valid : ptr<function, bool>) -> BodyPose {
    let delta = vec3<i32>(
        bounded_sector_delta(
            sim.worldSector.x, worldMeta.x, maximumHorizontalSectors),
        bounded_sector_delta(
            sim.worldSector.y, worldMeta.y, maximumVerticalSectors),
        bounded_sector_delta(
            sim.worldSector.z, worldMeta.z, maximumHorizontalSectors));
    let validValue = all(delta != vec3<i32>(2147483647));
    (*valid) = validValue;
    var result = pose;
    if (validValue) {
        result.position_invMass = vec4<f32>(
            pose.position_invMass.xyz + vec3<f32>(delta) * WORLD_SECTOR_SIZE,
            pose.position_invMass.w);
    }
    return result;
}

@compute @workgroup_size(1)
fn apply_commands(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    for (var counter = 2u; counter < 8u; counter += 1u) {
        atomicStore(&counters[counter], 0u);
    }
    atomicStore(&counters[15], 0u);
    let targetTick = atomicLoad(&counters[1]) + 1u;
    var appliedCommands = 0u;
    for (var commandIndex = 0u; commandIndex < sim.counts.y;
         commandIndex = commandIndex + 1u) {
        let command = commands[commandIndex];
        if (command.header.w != targetTick) { continue; }
        let commandType = command.header.x;
        let body = command.header.y;
        let generation = command.header.z;
        if (body == 0u || body >= sim.counts.x) { continue; }

        if (commandType == COMMAND_SPAWN) {
            if ((body_flags(body) & BODY_ALIVE) != 0u) { continue; }
            poses[body].position_invMass = command.p0;
            poses[body].orientation = normalize(command.p1);
            motions[body].linearVelocity_sleep = command.p2;
            motions[body].angularVelocity_flags = vec4<f32>(
                command.p3.xyz, 0.0);
            shapes[body].dimensions_type = command.p4;
            shapes[body].invInertia_material = vec4<f32>(shape_inverse_inertia(
                command.p4.xyz, u32(clamp(command.p4.w, 0.0, 4.0)),
                command.p0.w), bitcast<f32>(bitcast<u32>(command.p5.w)));
            shapes[body].material_coefficients = command.p6;
            forces[body] = vec4<f32>(0.0);
            let spawnFlags = BODY_ALIVE | BODY_AWAKE
                | select(0u, BODY_BULLET, command.p3.w > 0.5);
            metadata[body] = vec4<i32>(command.p5.xyz,
                pack_generation_flags(generation, spawnFlags));
            appliedCommands += 1u;
            continue;
        }
        if (!is_live(body, generation)
            || commandType > COMMAND_KINEMATIC_TARGET) { continue; }
        if (commandType == COMMAND_DESTROY) {
            var nextGeneration = (generation + 1u) & GENERATION_MASK;
            if (nextGeneration == 0u) { nextGeneration = 1u; }
            metadata[body] = vec4<i32>(
                0, 0, 0, pack_generation_flags(nextGeneration, 0u));
            let pose = poses[body];
            poses[body].position_invMass = vec4<f32>(
                pose.position_invMass.xyz, 0.0);
            shapes[body].dimensions_type = vec4<f32>(0.0);
            shapes[body].invInertia_material = vec4<f32>(0.0);
            shapes[body].material_coefficients = vec4<f32>(0.0);
            motions[body] = BodyMotion(vec4<f32>(0.0), vec4<f32>(0.0));
        } else if (commandType == COMMAND_IMPULSE) {
            let motion = motions[body];
            motions[body].linearVelocity_sleep = vec4<f32>(
                motion.linearVelocity_sleep.xyz
                    + command.p0.xyz * poses[body].position_invMass.w,
                0.0);
            set_body_flags(body, body_flags(body) | BODY_AWAKE);
        } else if (commandType == COMMAND_FORCE) {
            forces[body] = vec4<f32>(forces[body].xyz + command.p0.xyz, 0.0);
            motions[body].linearVelocity_sleep.w = 0.0;
            set_body_flags(body, body_flags(body) | BODY_AWAKE);
        } else if (commandType == COMMAND_SET_VELOCITY) {
            motions[body].linearVelocity_sleep = vec4<f32>(
                command.p0.xyz, 0.0);
            set_body_flags(body, body_flags(body) | BODY_AWAKE);
        } else if (commandType == COMMAND_SET_ANGULAR_VELOCITY) {
            motions[body].angularVelocity_flags = vec4<f32>(
                command.p0.xyz, motions[body].angularVelocity_flags.w);
            motions[body].linearVelocity_sleep.w = 0.0;
            set_body_flags(body, body_flags(body) | BODY_AWAKE);
        } else if (commandType == COMMAND_TELEPORT) {
            let oldPose = poses[body];
            poses[body].position_invMass = vec4<f32>(
                command.p0.xyz, oldPose.position_invMass.w);
            let qLength = length(command.p1);
            if (qLength > 1e-7) { poses[body].orientation = command.p1 / qLength; }
            metadata[body] = vec4<i32>(command.p5.xyz, metadata[body].w);
            set_body_flags(body, body_flags(body) | BODY_AWAKE);
        } else if (commandType == COMMAND_KINEMATIC_TARGET) {
            let oldPose = poses[body];
            let oldMetadata = metadata[body];
            var targetOrientation = oldPose.orientation;
            let qLength = length(command.p1);
            if (qLength > 1e-7) {
                targetOrientation = command.p1 / qLength;
            }

            let sectorDelta = vec3<i32>(
                bounded_sector_delta(oldMetadata.x, command.p5.x, 4096u),
                bounded_sector_delta(oldMetadata.y, command.p5.y, 4096u),
                bounded_sector_delta(oldMetadata.z, command.p5.z, 4096u));
            var linear = vec3<f32>(0.0);
            if (all(sectorDelta != vec3<i32>(2147483647))) {
                let translation = command.p0.xyz - oldPose.position_invMass.xyz
                    + vec3<f32>(sectorDelta) * WORLD_SECTOR_SIZE;
                linear = clamp_length(
                    translation / max(sim.gravity_dt.w, 1e-7),
                    sim.damping_clamps.z);
            }
            var deltaOrientation = quaternion_multiply(
                targetOrientation,
                vec4<f32>(-oldPose.orientation.xyz, oldPose.orientation.w));
            if (deltaOrientation.w < 0.0) {
                deltaOrientation = -deltaOrientation;
            }
            let sinHalfAngle = length(deltaOrientation.xyz);
            let angle = 2.0 * atan2(sinHalfAngle, deltaOrientation.w);
            var angular = vec3<f32>(0.0);
            if (sinHalfAngle > 1e-7) {
                angular = clamp_length(
                    deltaOrientation.xyz / sinHalfAngle
                        * (angle / max(sim.gravity_dt.w, 1e-7)),
                    sim.damping_clamps.w);
            }

            poses[body].position_invMass = vec4<f32>(command.p0.xyz, 0.0);
            poses[body].orientation = targetOrientation;
            shapes[body].invInertia_material = vec4<f32>(
                vec3<f32>(0.0), shapes[body].invInertia_material.w);
            motions[body].linearVelocity_sleep = vec4<f32>(linear, 0.0);
            motions[body].angularVelocity_flags = vec4<f32>(
                angular, bitcast<f32>(targetTick));
            metadata[body] = vec4<i32>(command.p5.xyz, oldMetadata.w);
            set_body_flags(body,
                body_flags(body) | BODY_AWAKE | BODY_KINEMATIC);
        } else if (commandType == COMMAND_SET_MATERIAL) {
            shapes[body].invInertia_material.w =
                bitcast<f32>(bitcast<u32>(command.p5.w));
            shapes[body].material_coefficients = command.p6;
        } else if (commandType == COMMAND_WAKE) {
            motions[body].linearVelocity_sleep.w = 0.0;
            set_body_flags(body, body_flags(body) | BODY_AWAKE);
        } else if (commandType == COMMAND_SLEEP) {
            let sleepingFlags = body_flags(body) & ~BODY_AWAKE;
            set_body_flags(body, sleepingFlags);
            let motion = motions[body];
            motions[body].linearVelocity_sleep = vec4<f32>(
                vec3<f32>(0.0), motion.linearVelocity_sleep.w);
            motions[body].angularVelocity_flags = vec4<f32>(
                vec3<f32>(0.0), motion.angularVelocity_flags.w);
            forces[body] = vec4<f32>(0.0);
        }
        appliedCommands += 1u;
    }
    atomicStore(&counters[7], appliedCommands);
    atomicMax(&counters[13], appliedCommands);
}

var<workgroup> scanScratch : array<u32, 256>;

@compute @workgroup_size(256)
fn compact_blocks(@builtin(global_invocation_id) gid : vec3<u32>,
                  @builtin(local_invocation_id) lid : vec3<u32>,
                  @builtin(workgroup_id) group : vec3<u32>) {
    let body = gid.x;
    var predicate = 0u;
    if (body < sim.counts.x) {
        let flags = body_flags(body);
        if ((flags & (BODY_ALIVE | BODY_KINEMATIC))
                == (BODY_ALIVE | BODY_KINEMATIC)) {
            atomicAdd(&counters[15], 1u);
        }
        predicate = select(0u, 1u,
            (flags & (BODY_ALIVE | BODY_AWAKE)) == (BODY_ALIVE | BODY_AWAKE));
    }
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
    if (body < sim.counts.x) {
        activeOffsets[body] = scanScratch[lid.x] - predicate;
    }
    if (lid.x == WORKGROUP_SIZE - 1u) {
        blockSums[group.x] = scanScratch[lid.x];
    }
}

@compute @workgroup_size(1)
fn scan_blocks(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    var sum = 0u;
    for (var block = 0u; block < sim.counts.z; block = block + 1u) {
        blockPrefix[block] = sum;
        sum += blockSums[block];
    }
    let activeCapacity = select(
        sim.counts.x, sim.debugRange.z, sim.debugRange.z != 0u);
    let activeCount = min(sum, activeCapacity);
    atomicStore(&counters[0], activeCount);
    atomicMax(&counters[8], activeCount);
    atomicStore(&counters[14], select(0u, 1u, sum > activeCapacity));
}

@compute @workgroup_size(256)
fn scatter_active(@builtin(global_invocation_id) gid : vec3<u32>,
                  @builtin(workgroup_id) group : vec3<u32>) {
    let body = gid.x;
    if (body >= sim.counts.x) { return; }
    let flags = body_flags(body);
    if ((flags & (BODY_ALIVE | BODY_AWAKE)) != (BODY_ALIVE | BODY_AWAKE)) {
        return;
    }
    let output = blockPrefix[group.x] + activeOffsets[body];
    if (output < atomicLoad(&counters[0])) { activeBodyIds[output] = body; }
}

fn quaternion_multiply(a : vec4<f32>, b : vec4<f32>) -> vec4<f32> {
    return vec4<f32>(
        a.w * b.xyz + b.w * a.xyz + cross(a.xyz, b.xyz),
        a.w * b.w - dot(a.xyz, b.xyz));
}

fn rotate_by_quaternion(q : vec4<f32>, value : vec3<f32>) -> vec3<f32> {
    let twiceCross = 2.0 * cross(q.xyz, value);
    return value + q.w * twiceCross + cross(q.xyz, twiceCross);
}

fn inverse_rotate_by_quaternion(q : vec4<f32>, value : vec3<f32>) -> vec3<f32> {
    return rotate_by_quaternion(vec4<f32>(-q.xyz, q.w), value);
}

fn clamp_length(value : vec3<f32>, maximum : f32) -> vec3<f32> {
    let squared = dot(value, value);
    if (squared > maximum * maximum && squared > 0.0) {
        return value * (maximum * inverseSqrt(squared));
    }
    return value;
}

fn shape_inverse_inertia(dimensionsInput : vec3<f32>, shapeType : u32,
                         inverseMass : f32) -> vec3<f32> {
    if (inverseMass <= 0.0) { return vec3<f32>(0.0); }
    let dimensions = max(abs(dimensionsInput), vec3<f32>(1e-4));
    if (shapeType == SHAPE_SPHERE) {
        let radius = 0.5 * dimensions.x;
        return vec3<f32>(inverseMass / max(0.4 * radius * radius, 1e-7));
    }
    if (shapeType == SHAPE_CAPSULE) {
        let radius = 0.25 * (dimensions.x + dimensions.z);
        let cylinderLength = max(dimensions.y - 2.0 * radius, 0.0);
        let denominator = cylinderLength + (4.0 / 3.0) * radius;
        let cylinderFraction = cylinderLength / max(denominator, 1e-7);
        let capFraction = 1.0 - cylinderFraction;
        let axial = cylinderFraction * 0.5 * radius * radius
            + capFraction * 0.4 * radius * radius;
        let capCenter = 0.5 * cylinderLength + 0.375 * radius;
        let transverse = cylinderFraction
                * (3.0 * radius * radius
                    + cylinderLength * cylinderLength) / 12.0
            + capFraction
                * ((83.0 / 320.0) * radius * radius
                    + capCenter * capCenter);
        return vec3<f32>(
            inverseMass / max(transverse, 1e-7),
            inverseMass / max(axial, 1e-7),
            inverseMass / max(transverse, 1e-7));
    }
    if (shapeType == SHAPE_CYLINDER) {
        let radius = 0.25 * (dimensions.x + dimensions.z);
        let height = dimensions.y;
        let inverseXZ = inverseMass
            / max((3.0 * radius * radius + height * height) / 12.0, 1e-7);
        let inverseY = inverseMass / max(0.5 * radius * radius, 1e-7);
        return vec3<f32>(inverseXZ, inverseY, inverseXZ);
    }
    return vec3<f32>(
        12.0 * inverseMass / max(dimensions.y * dimensions.y
                               + dimensions.z * dimensions.z, 1e-7),
        12.0 * inverseMass / max(dimensions.x * dimensions.x
                               + dimensions.z * dimensions.z, 1e-7),
        12.0 * inverseMass / max(dimensions.x * dimensions.x
                               + dimensions.y * dimensions.y, 1e-7));
}

fn inverse_inertia_world(orientation : vec4<f32>, inverseInertia : vec3<f32>,
                         torque : vec3<f32>) -> vec3<f32> {
    let localTorque = inverse_rotate_by_quaternion(orientation, torque);
    return rotate_by_quaternion(orientation, localTorque * inverseInertia);
}

fn shape_bounding_radius(shape : BodyShape) -> f32 {
    let dimensions = max(abs(shape.dimensions_type.xyz), vec3<f32>(1e-5));
    let shapeType = u32(clamp(shape.dimensions_type.w, 0.0, 4.0));
    if (shapeType == SHAPE_SPHERE) { return 0.5 * dimensions.x; }
    if (shapeType == SHAPE_CAPSULE) {
        let radius = 0.25 * (dimensions.x + dimensions.z);
        return max(0.5 * dimensions.y - radius, 0.0) + radius;
    }
    if (shapeType == SHAPE_CYLINDER) {
        let radius = 0.25 * (dimensions.x + dimensions.z);
        return length(vec2<f32>(radius, 0.5 * dimensions.y));
    }
    return 0.5 * length(dimensions);
}

fn pack_shape_type_sweep(shapeType : u32, sweepDistance : f32) -> f32 {
    let fraction = clamp(sweepDistance / SHAPE_SWEEP_RANGE,
                         0.0, SHAPE_SWEEP_MAX_FRACTION);
    return f32(shapeType) + fraction;
}

fn shape_vertical_extent(shape : BodyShape, orientation : vec4<f32>) -> f32 {
    let dimensions = max(abs(shape.dimensions_type.xyz), vec3<f32>(1e-5));
    let shapeType = u32(clamp(shape.dimensions_type.w, 0.0, 4.0));
    if (shapeType == SHAPE_SPHERE) { return 0.5 * dimensions.x; }
    let axisX = rotate_by_quaternion(orientation, vec3<f32>(1.0, 0.0, 0.0));
    let axisY = rotate_by_quaternion(orientation, vec3<f32>(0.0, 1.0, 0.0));
    let axisZ = rotate_by_quaternion(orientation, vec3<f32>(0.0, 0.0, 1.0));
    if (shapeType == SHAPE_CAPSULE) {
        let radius = 0.25 * (dimensions.x + dimensions.z);
        let segmentHalf = max(0.5 * dimensions.y - radius, 0.0);
        return radius + segmentHalf * abs(axisY.y);
    }
    if (shapeType == SHAPE_CYLINDER) {
        let radius = 0.25 * (dimensions.x + dimensions.z);
        let radialY = sqrt(max(1.0 - axisY.y * axisY.y, 0.0));
        return 0.5 * dimensions.y * abs(axisY.y) + radius * radialY;
    }
    let half = 0.5 * dimensions;
    return dot(vec3<f32>(abs(axisX.y), abs(axisY.y), abs(axisZ.y)), half);
}

fn raw_height_to_world(rawHeight : f32) -> f32 {
    return sim.terrainOrigin_cell_height.w
        * (2.0 * rawHeight / 65535.0 - 1.0);
}

fn terrain_raw_base(coordinate : vec2<i32>) -> f32 {
    let maximum = vec2<i32>(sim.terrainSize_mips_flags.xy) - vec2<i32>(1);
    return f32(textureLoad(maxHeightTexture,
                           clamp(coordinate, vec2<i32>(0), maximum), 0).x);
}

fn terrain_vertex_gradient(coordinate : vec2<i32>) -> vec2<f32> {
    let maximum = vec2<i32>(sim.terrainSize_mips_flags.xy) - vec2<i32>(1);
    let left = max(coordinate.x - 1, 0);
    let right = min(coordinate.x + 1, maximum.x);
    let top = max(coordinate.y - 1, 0);
    let bottom = min(coordinate.y + 1, maximum.y);
    let heightFactor = 2.0 * sim.terrainOrigin_cell_height.w / 65535.0;
    let cellScale = sim.terrainOrigin_cell_height.z;
    let dxDenominator = max(f32(right - left) * cellScale, 1e-7);
    let dzDenominator = max(f32(bottom - top) * cellScale, 1e-7);
    let dx = (terrain_raw_base(vec2<i32>(right, coordinate.y))
            - terrain_raw_base(vec2<i32>(left, coordinate.y)))
            * heightFactor / dxDenominator;
    let dz = (terrain_raw_base(vec2<i32>(coordinate.x, bottom))
            - terrain_raw_base(vec2<i32>(coordinate.x, top)))
            * heightFactor / dzDenominator;
    return vec2<f32>(dx, dz);
}

struct TerrainSurface {
    height : f32,
    normal : vec3<f32>,
    featureBase : u32,
    valid : u32,
};

fn terrain_surface(worldXZ : vec2<f32>) -> TerrainSurface {
    var result : TerrainSurface;
    result.height = 0.0;
    result.normal = vec3<f32>(0.0, 1.0, 0.0);
    result.featureBase = 0u;
    result.valid = 0u;
    if (sim.terrainSize_mips_flags.w == 0u
        || sim.terrainSize_mips_flags.x < 2u
        || sim.terrainSize_mips_flags.y < 2u) { return result; }

    let local = (worldXZ + sim.terrainOrigin_cell_height.xy)
        / sim.terrainOrigin_cell_height.z;
    let maximum = vec2<f32>(sim.terrainSize_mips_flags.xy - vec2<u32>(1u));
    if (any(local < vec2<f32>(0.0)) || any(local > maximum)) { return result; }

    let cell = min(vec2<u32>(floor(local)),
                   sim.terrainSize_mips_flags.xy - vec2<u32>(2u));
    let fraction = clamp(local - vec2<f32>(cell),
                         vec2<f32>(0.0), vec2<f32>(1.0));
    let coordinate = vec2<i32>(cell);
    let topLeft = terrain_raw_base(coordinate);
    let topRight = terrain_raw_base(coordinate + vec2<i32>(1, 0));
    let bottomLeft = terrain_raw_base(coordinate + vec2<i32>(0, 1));
    let bottomRight = terrain_raw_base(coordinate + vec2<i32>(1, 1));
    var rawHeight = 0.0;
    var triangle = 0u;
    if (fraction.y >= fraction.x) {
        rawHeight = topLeft
            + fraction.x * (bottomRight - bottomLeft)
            + fraction.y * (bottomLeft - topLeft);
    } else {
        triangle = 1u;
        rawHeight = topLeft
            + fraction.x * (topRight - topLeft)
            + fraction.y * (bottomRight - topRight);
    }

    let gradientTopLeft = terrain_vertex_gradient(coordinate);
    let gradientTopRight = terrain_vertex_gradient(coordinate + vec2<i32>(1, 0));
    let gradientBottomLeft = terrain_vertex_gradient(coordinate + vec2<i32>(0, 1));
    let gradientBottomRight = terrain_vertex_gradient(coordinate + vec2<i32>(1, 1));
    let gradient = mix(mix(gradientTopLeft, gradientTopRight, fraction.x),
                       mix(gradientBottomLeft, gradientBottomRight, fraction.x),
                       fraction.y);
    result.height = raw_height_to_world(rawHeight);
    result.normal = normalize(vec3<f32>(-gradient.x, 1.0, -gradient.y));
    result.featureBase = ((cell.y * sim.terrainSize_mips_flags.x + cell.x) * 2u
                        + triangle) * MAX_TERRAIN_CANDIDATES;
    result.valid = 1u;
    return result;
}

fn terrain_max_mip_reject(position : vec3<f32>, radius : f32,
                          minimumY : f32) -> bool {
    if (sim.terrainSize_mips_flags.w == 0u) { return false; }
    let terrainMaximum = vec2<f32>(
        sim.terrainSize_mips_flags.xy - vec2<u32>(1u));
    let minimumSample = (position.xz - vec2<f32>(radius)
                       + sim.terrainOrigin_cell_height.xy)
                       / sim.terrainOrigin_cell_height.z;
    let maximumSample = (position.xz + vec2<f32>(radius)
                       + sim.terrainOrigin_cell_height.xy)
                       / sim.terrainOrigin_cell_height.z;
    if (any(maximumSample < vec2<f32>(0.0))
        || any(minimumSample > terrainMaximum)) { return true; }
    let clampedMinimum = vec2<u32>(floor(clamp(
        minimumSample, vec2<f32>(0.0), terrainMaximum)));
    let clampedMaximum = vec2<u32>(floor(clamp(
        maximumSample, vec2<f32>(0.0), terrainMaximum)));
    let span = clampedMaximum - clampedMinimum + vec2<u32>(1u);
    var level = 0u;
    var scale = 1u;
    let mipCount = max(sim.terrainSize_mips_flags.z, 1u);
    loop {
        if (level + 1u >= mipCount
            || ((span.x + scale - 1u) / scale <= 3u
                && (span.y + scale - 1u) / scale <= 3u)) { break; }
        level += 1u;
        scale *= 2u;
    }
    let dimensions = textureDimensions(maxHeightTexture, i32(level));
    let mipMinimum = min(clampedMinimum / scale, dimensions - vec2<u32>(1u));
    let mipMaximum = min(clampedMaximum / scale, dimensions - vec2<u32>(1u));
    var maximumRaw = 0u;
    for (var z = 0u; z < 4u; z += 1u) {
        for (var x = 0u; x < 4u; x += 1u) {
            let coordinate = mipMinimum + vec2<u32>(x, z);
            if (all(coordinate <= mipMaximum)) {
                maximumRaw = max(maximumRaw, textureLoad(
                    maxHeightTexture, vec2<i32>(coordinate), i32(level)).x);
            }
        }
    }
    return minimumY > raw_height_to_world(f32(maximumRaw)) + sim.solver.x;
}

struct TerrainCandidate {
    point : vec3<f32>,
    separation : f32,
    normal : vec3<f32>,
    featureId : u32,
};

struct TerrainCandidateSet {
    items : array<TerrainCandidate, 16>,
    count : u32,
    rejected : u32,
};

struct TerrainContacts {
    items : array<TerrainCandidate, 4>,
    count : u32,
    rejected : u32,
};

fn append_terrain_candidate(candidateSet : ptr<function, TerrainCandidateSet>,
                            point : vec3<f32>, localFeature : u32) {
    if ((*candidateSet).count >= MAX_TERRAIN_CANDIDATES) { return; }
    let surface = terrain_surface(point.xz);
    if (surface.valid == 0u) { return; }
    let terrainPoint = vec3<f32>(point.x, surface.height, point.z);
    let separation = dot(point - terrainPoint, surface.normal);
    if (separation > sim.solver.x) { return; }
    let output = (*candidateSet).count;
    (*candidateSet).items[output].point = point;
    (*candidateSet).items[output].separation = separation;
    (*candidateSet).items[output].normal = surface.normal;
    (*candidateSet).items[output].featureId =
        surface.featureBase + localFeature + 1u;
    (*candidateSet).count = output + 1u;
}

fn append_sphere_terrain_candidate(
                                   candidateSet : ptr<function, TerrainCandidateSet>,
                                   center : vec3<f32>, radius : f32,
                                   localFeature : u32) {
    let firstSurface = terrain_surface(center.xz);
    if (firstSurface.valid == 0u) { return; }
    let point = center - firstSurface.normal * radius;
    append_terrain_candidate(candidateSet, point, localFeature);
}

fn generate_terrain_candidates(pose : BodyPose,
                               shape : BodyShape) -> TerrainCandidateSet {
    var result : TerrainCandidateSet;
    result.count = 0u;
    result.rejected = 0u;
    if (sim.terrainSize_mips_flags.w == 0u) { return result; }
    let radius = shape_bounding_radius(shape);
    if (terrain_max_mip_reject(pose.position_invMass.xyz, radius,
                               pose.position_invMass.y - radius)) {
        result.rejected = 1u;
        return result;
    }

    let dimensions = max(abs(shape.dimensions_type.xyz), vec3<f32>(1e-5));
    let half = 0.5 * dimensions;
    let shapeType = u32(clamp(shape.dimensions_type.w, 0.0, 4.0));
    if (shapeType == SHAPE_SPHERE) {
        append_sphere_terrain_candidate(&result, pose.position_invMass.xyz,
                                        half.x, 0u);
    } else if (shapeType == SHAPE_CAPSULE) {
        let capsuleRadius = 0.25 * (dimensions.x + dimensions.z);
        let segmentHalf = max(half.y - capsuleRadius, 0.0);
        let axis = rotate_by_quaternion(
            pose.orientation, vec3<f32>(0.0, segmentHalf, 0.0));
        append_sphere_terrain_candidate(
            &result, pose.position_invMass.xyz - axis, capsuleRadius, 0u);
        append_sphere_terrain_candidate(
            &result, pose.position_invMass.xyz, capsuleRadius, 1u);
        append_sphere_terrain_candidate(
            &result, pose.position_invMass.xyz + axis, capsuleRadius, 2u);
    } else if (shapeType == SHAPE_CYLINDER) {
        let cylinderRadius = 0.25 * (dimensions.x + dimensions.z);
        for (var ring = 0u; ring < 2u; ring += 1u) {
            let localY = select(-half.y, half.y, ring == 1u);
            for (var vertex = 0u; vertex < 8u; vertex += 1u) {
                let angle = 0.7853981633974483 * f32(vertex);
                let localPoint = vec3<f32>(cos(angle) * cylinderRadius,
                                           localY,
                                           sin(angle) * cylinderRadius);
                append_terrain_candidate(
                    &result, pose.position_invMass.xyz
                           + rotate_by_quaternion(pose.orientation, localPoint),
                    ring * 8u + vertex);
            }
        }
    } else {
        for (var corner = 0u; corner < 8u; corner += 1u) {
            let signs = vec3<f32>(
                select(-1.0, 1.0, (corner & 1u) != 0u),
                select(-1.0, 1.0, (corner & 2u) != 0u),
                select(-1.0, 1.0, (corner & 4u) != 0u));
            let point = pose.position_invMass.xyz
                + rotate_by_quaternion(pose.orientation, signs * half);
            append_terrain_candidate(&result, point, corner);
        }
        var facePoints = array<vec3<f32>, 6>(
            vec3<f32>(-half.x, 0.0, 0.0),
            vec3<f32>( half.x, 0.0, 0.0),
            vec3<f32>(0.0, -half.y, 0.0),
            vec3<f32>(0.0,  half.y, 0.0),
            vec3<f32>(0.0, 0.0, -half.z),
            vec3<f32>(0.0, 0.0,  half.z));
        for (var face = 0u; face < 6u; face += 1u) {
            let point = pose.position_invMass.xyz
                + rotate_by_quaternion(pose.orientation, facePoints[face]);
            append_terrain_candidate(&result, point, 8u + face);
        }
    }
    return result;
}

fn candidate_less(a : TerrainCandidate, b : TerrainCandidate) -> bool {
    return a.separation < b.separation
        || (a.separation == b.separation && a.featureId < b.featureId);
}

fn reduce_terrain_candidates(input : TerrainCandidateSet) -> TerrainContacts {
    var source = input;
    var result : TerrainContacts;
    result.count = 0u;
    result.rejected = input.rejected;
    if (source.count == 0u) { return result; }
    if (source.count == 1u) {
        result.items[0] = source.items[0];
        result.count = 1u;
        return result;
    }

    var deepestIndex = 0u;
    for (var index = 1u; index < source.count; index += 1u) {
        if (candidate_less(source.items[index], source.items[deepestIndex])) {
            deepestIndex = index;
        }
    }
    if (deepestIndex != 0u) {
        let deepest = source.items[deepestIndex];
        source.items[deepestIndex] = source.items[0];
        source.items[0] = deepest;
    }

    var selected = array<u32, 4>(0xffffffffu, 0xffffffffu,
                                  0xffffffffu, 0xffffffffu);
    selected[0] = 0u;
    result.items[0] = source.items[0];
    result.count = 1u;
    let outputCount = min(source.count, MAX_TERRAIN_CONTACTS);
    for (var output = 1u; output < outputCount; output += 1u) {
        var bestIndex = 0xffffffffu;
        var bestScore = -1.0;
        for (var candidate = 1u; candidate < source.count; candidate += 1u) {
            var alreadySelected = false;
            for (var prior = 0u; prior < output; prior += 1u) {
                alreadySelected = alreadySelected || selected[prior] == candidate;
            }
            if (alreadySelected) { continue; }
            var minimumDistance = 3.402823466e+38;
            for (var prior = 0u; prior < output; prior += 1u) {
                let delta = source.items[candidate].point.xz
                    - source.items[selected[prior]].point.xz;
                minimumDistance = min(minimumDistance, dot(delta, delta));
            }
            if (minimumDistance > bestScore
                || (minimumDistance == bestScore
                    && (bestIndex == 0xffffffffu
                        || source.items[candidate].featureId
                           < source.items[bestIndex].featureId))) {
                bestIndex = candidate;
                bestScore = minimumDistance;
            }
        }
        if (bestIndex == 0xffffffffu) { break; }
        selected[output] = bestIndex;
        result.items[output] = source.items[bestIndex];
        result.count = output + 1u;
    }
    return result;
}

struct BodyVelocities {
    linear : vec3<f32>,
    angular : vec3<f32>,
};

fn apply_body_impulse(velocities : BodyVelocities, impulse : vec3<f32>,
                      leverArm : vec3<f32>, inverseMass : f32,
                      inverseInertia : vec3<f32>,
                      orientation : vec4<f32>) -> BodyVelocities {
    var result = velocities;
    result.linear += impulse * inverseMass;
    result.angular += inverse_inertia_world(
        orientation, inverseInertia, cross(leverArm, impulse));
    return result;
}

fn contact_effective_mass(leverArm : vec3<f32>, direction : vec3<f32>,
                          inverseMass : f32, inverseInertia : vec3<f32>,
                          orientation : vec4<f32>) -> f32 {
    let angular = inverse_inertia_world(
        orientation, inverseInertia, cross(leverArm, direction));
    let denominator = inverseMass + dot(cross(angular, leverArm), direction);
    return select(0.0, 1.0 / denominator, denominator > 1e-7);
}

fn cached_normal_impulse(cache : TerrainContactCache,
                         featureId : u32) -> f32 {
    for (var index = 0u; index < MAX_TERRAIN_CONTACTS; index += 1u) {
        if (cache.featureIds[index] == featureId) {
            return max(cache.normalImpulses[index], 0.0);
        }
    }
    return 0.0;
}

struct GpuWaterSurface {
    heightOffset : f32,
    normal : vec3<f32>,
};

fn physics_long_wave(position : vec2<f32>, direction : vec2<f32>,
                     wavelength : f32, phaseOffset : f32, omega : f32,
                     time : f32, strength : f32) -> vec4<f32> {
    let waveNumber = 6.283185307179586 / wavelength;
    let phase = waveNumber * dot(direction, position) -
                omega * time + phaseOffset;
    let amplitude = 5.1541 * strength;
    let ka = waveNumber * amplitude;
    return vec4<f32>(
        amplitude * cos(phase), direction.x * ka * sin(phase),
        -ka * cos(phase), direction.y * ka * sin(phase));
}

fn water_cascade_uv(localXZ : vec2<f32>, sectorXZ : vec2<i32>,
                    cascade : u32) -> vec2<f32> {
    let patchLength = select(1949.0, 326.0, cascade == 1u);
    // Bound the f32 conversion while keeping normal play-space positions
    // continuous. Texture repeat performs the final spectral-period wrap.
    let wrappedSector = sectorXZ % vec2<i32>(8192);
    return (localXZ + vec2<f32>(wrappedSector) * WORLD_SECTOR_SIZE)
        / patchLength + vec2<f32>(0.5 + 0.5 / 256.0);
}

fn sample_gpu_water_surface(pose : BodyPose,
                            worldMeta : vec4<i32>) -> GpuWaterSurface {
    var result : GpuWaterSurface;
    result.heightOffset = 0.0;
    result.normal = vec3<f32>(0.0, 1.0, 0.0);
    let strength = max(sim.waterSurface.x, 0.0);
    if (sim.water.y < 0.5 || strength <= 0.0) { return result; }

    let localXZ = pose.position_invMass.xz;
    let sectorXZ = worldMeta.xz;
    let broad = textureSampleLevel(
        waterDisplacementTexture, waterDisplacementSampler,
        water_cascade_uv(localXZ, sectorXZ, 0u), 0, 0.0);
    let detail = textureSampleLevel(
        waterDisplacementTexture, waterDisplacementSampler,
        water_cascade_uv(localXZ, sectorXZ, 1u), 1, 0.0);
    let broadNormal = textureSampleLevel(
        waterDisplacementTexture, waterDisplacementSampler,
        water_cascade_uv(localXZ, sectorXZ, 0u), 2, 0.0).xyz;
    let detailNormal = textureSampleLevel(
        waterDisplacementTexture, waterDisplacementSampler,
        water_cascade_uv(localXZ, sectorXZ, 1u), 3, 0.0).xyz;
    let worldXZ = localXZ + vec2<f32>(sectorXZ % vec2<i32>(8192)) *
                  WORLD_SECTOR_SIZE;
    let longA = physics_long_wave(
        worldXZ, vec2<f32>(0.923059017, 0.384658357), 440.298507,
        0.000000000, 0.374291312, sim.waterSurface.y, strength);
    let longB = physics_long_wave(
        worldXZ, vec2<f32>(0.700400636, 0.713749921), 701.258144,
        5.553108549, 0.296825282, sim.waterSurface.y, strength);
    let longC = physics_long_wave(
        worldXZ, vec2<f32>(0.367164395, 0.930156066), 1116.885424,
        4.823031791, 0.234699061, sim.waterSurface.y, strength);
    let longD = physics_long_wave(
        worldXZ, vec2<f32>(-0.024039031, 0.999711021), 1778.85,
        4.092955033, 0.186378666, sim.waterSurface.y, strength);
    let longWaves = longA + longB + longC + longD;
    result.heightOffset = (broad.y + detail.y) * strength + longWaves.x;
    result.normal = normalize(
        vec3<f32>(0.0, 1.0, 0.0) +
        (broadNormal - vec3<f32>(0.0, 1.0, 0.0) +
         detailNormal - vec3<f32>(0.0, 1.0, 0.0)) * strength +
        longWaves.yzw);
    return result;
}

struct WaterSampleAccumulator {
    weightedSubmersion : f32,
    totalWeight : f32,
    weightedCenter : vec3<f32>,
    submergedWeight : f32,
};

struct WaterState {
    fraction : f32,
    buoyancyCenter : vec3<f32>,
    normal : vec3<f32>,
};

fn append_water_sample(accumulator : ptr<function, WaterSampleAccumulator>,
                       pose : BodyPose, localPoint : vec3<f32>,
                       sampleRadius : f32, weight : f32,
                       waterHeight : f32) {
    let point = pose.position_invMass.xyz
        + rotate_by_quaternion(pose.orientation, localPoint);
    let radius = max(sampleRadius, 1e-4);
    let submersion = clamp(
        (waterHeight - point.y + radius) / (2.0 * radius), 0.0, 1.0);
    (*accumulator).weightedSubmersion += submersion * weight;
    (*accumulator).totalWeight += weight;
    (*accumulator).weightedCenter += point * submersion * weight;
    (*accumulator).submergedWeight += submersion * weight;
}

fn sample_water_state_in_frame(pose : BodyPose,
                               shape : BodyShape,
                               surface : GpuWaterSurface) -> WaterState {
    var result : WaterState;
    result.fraction = 0.0;
    result.buoyancyCenter = pose.position_invMass.xyz;
    result.normal = surface.normal;
    if (sim.water.y < 0.5) { return result; }

    let dimensions = max(abs(shape.dimensions_type.xyz), vec3<f32>(1e-5));
    let half = 0.5 * dimensions;
    let shapeType = u32(clamp(shape.dimensions_type.w, 0.0, 4.0));
    var accumulator : WaterSampleAccumulator;
    accumulator.weightedSubmersion = 0.0;
    accumulator.totalWeight = 0.0;
    accumulator.weightedCenter = vec3<f32>(0.0);
    accumulator.submergedWeight = 0.0;
    if (shapeType == SHAPE_SPHERE) {
        let radius = half.x;
        append_water_sample(&accumulator, pose,
                            vec3<f32>(0.0, -0.5 * radius, 0.0),
                            0.5 * radius, 1.0,
                            sim.water.x + surface.heightOffset);
        append_water_sample(&accumulator, pose, vec3<f32>(0.0),
                            0.5 * radius, 1.0,
                            sim.water.x + surface.heightOffset);
        append_water_sample(&accumulator, pose,
                            vec3<f32>(0.0, 0.5 * radius, 0.0),
                            0.5 * radius, 1.0,
                            sim.water.x + surface.heightOffset);
    } else if (shapeType == SHAPE_CAPSULE) {
        let radius = 0.25 * (dimensions.x + dimensions.z);
        let segmentHalf = max(half.y - radius, 0.0);
        append_water_sample(&accumulator, pose,
                            vec3<f32>(0.0, -segmentHalf, 0.0), radius, 1.0,
                            sim.water.x + surface.heightOffset);
        append_water_sample(&accumulator, pose, vec3<f32>(0.0), radius, 1.0,
                            sim.water.x + surface.heightOffset);
        append_water_sample(&accumulator, pose,
                            vec3<f32>(0.0, segmentHalf, 0.0), radius, 1.0,
                            sim.water.x + surface.heightOffset);
    } else if (shapeType == SHAPE_CYLINDER) {
        let radius = 0.25 * (dimensions.x + dimensions.z);
        let sampleRadius = max(min(radius, half.y) * 0.5, 1e-4);
        for (var cap = 0u; cap < 2u; cap += 1u) {
            let localY = select(-half.y, half.y, cap == 1u);
            for (var vertex = 0u; vertex < 4u; vertex += 1u) {
                let angle = 1.5707963267948966 * f32(vertex);
                append_water_sample(&accumulator, pose,
                    vec3<f32>(cos(angle) * radius, localY,
                              sin(angle) * radius), sampleRadius, 1.0,
                    sim.water.x + surface.heightOffset);
            }
        }
    } else {
        let sampleRadius = max(
            min(dimensions.x, min(dimensions.y, dimensions.z)) * 0.2, 1e-4);
        for (var corner = 0u; corner < 8u; corner += 1u) {
            let signs = vec3<f32>(
                select(-1.0, 1.0, (corner & 1u) != 0u),
                select(-1.0, 1.0, (corner & 2u) != 0u),
                select(-1.0, 1.0, (corner & 4u) != 0u));
            append_water_sample(&accumulator, pose, signs * half,
                                sampleRadius, 1.0,
                                sim.water.x + surface.heightOffset);
        }
    }

    let extent = max(shape_vertical_extent(shape, pose.orientation), 1e-5);
    let analyticFraction = clamp(
        (sim.water.x + surface.heightOffset
            - (pose.position_invMass.y - extent)) / (2.0 * extent),
        0.0, 1.0);
    let sampledFraction = accumulator.weightedSubmersion
        / max(accumulator.totalWeight, 1e-5);
    result.fraction = clamp(
        0.5 * analyticFraction + 0.5 * sampledFraction, 0.0, 1.0);
    if (accumulator.submergedWeight > 1e-5) {
        result.buoyancyCenter = accumulator.weightedCenter
            / accumulator.submergedWeight;
    }
    return result;
}

fn sample_water_state(pose : BodyPose, shape : BodyShape,
                      worldMeta : vec4<i32>,
                      surface : GpuWaterSurface) -> WaterState {
    var result : WaterState;
    result.fraction = 0.0;
    result.buoyancyCenter = pose.position_invMass.xyz;
    result.normal = surface.normal;
    if (sim.water.y < 0.5) { return result; }

    let sectorDelta = bounded_sector_delta(
        sim.worldSector.y, worldMeta.y, 1u);
    if (sectorDelta == 2147483647) {
        // An infinite water plane is unambiguously above or below a body that
        // is more than one vertical sector away. Avoid a lossy large float.
        result.fraction = select(
            1.0, 0.0, worldMeta.y > sim.worldSector.y);
        return result;
    }

    var waterPose = pose;
    let frameOffset = f32(sectorDelta) * WORLD_SECTOR_SIZE;
    waterPose.position_invMass = vec4<f32>(
        pose.position_invMass.xyz + vec3<f32>(0.0, frameOffset, 0.0),
        pose.position_invMass.w);
    let framed = sample_water_state_in_frame(waterPose, shape, surface);
    result.fraction = framed.fraction;
    result.buoyancyCenter = framed.buoyancyCenter
        - vec3<f32>(0.0, frameOffset, 0.0);
    return result;
}

fn terrain_horizontal_sector_radius(shape : BodyShape) -> u32 {
    let width = f32(max(sim.terrainSize_mips_flags.x, 1u) - 1u);
    let height = f32(max(sim.terrainSize_mips_flags.y, 1u) - 1u);
    let halfSpan = 0.5 * max(width, height)
        * max(sim.terrainOrigin_cell_height.z, 0.0);
    return u32(ceil((halfSpan + shape_bounding_radius(shape))
                    / WORLD_SECTOR_SIZE)) + 1u;
}

fn terrain_vertical_sector_radius(shape : BodyShape) -> u32 {
    return u32(ceil((abs(sim.terrainOrigin_cell_height.w)
                    + shape_bounding_radius(shape))
                    / WORLD_SECTOR_SIZE)) + 1u;
}

fn water_sample_count(shapeType : u32) -> u32 {
    return select(8u, 3u,
        shapeType == SHAPE_SPHERE || shapeType == SHAPE_CAPSULE);
}

@compute @workgroup_size(256)
fn integrate_bodies(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x >= atomicLoad(&counters[0])) { return; }
    let body = activeBodyIds[gid.x];
    if ((body_flags(body) & (BODY_ALIVE | BODY_AWAKE))
        != (BODY_ALIVE | BODY_AWAKE)) { return; }

    var pose = poses[body];
    var motion = motions[body];
    let shape = shapes[body];
    var cache = terrainContactCaches[body];
    if (cache.state.x != metadata_generation(body)) {
        cache.normalImpulses = vec4<f32>(0.0);
        cache.featureIds = vec4<u32>(0u);
        cache.state = vec4<u32>(metadata_generation(body), 0u, 0u, 0u);
    }
    let substeps = max(sim.counts.w, 1u);
    let dt = sim.gravity_dt.w / f32(substeps);
    let linearScale = 1.0 / (1.0 + sim.damping_clamps.x * dt);
    let angularScale = 1.0 / (1.0 + sim.damping_clamps.y * dt);
    let inverseMass = pose.position_invMass.w;
    let shapeType = u32(clamp(shape.dimensions_type.w, 0.0, 4.0));
    let inverseInertia = shape_inverse_inertia(
        shape.dimensions_type.xyz, shapeType, inverseMass);
    let dryAcceleration = sim.gravity_dt.xyz + forces[body].xyz * inverseMass;
    var lastFeatures = vec4<u32>(0u);
    var lastImpulses = vec4<f32>(0.0);
    var lastContactCount = 0u;
    var rejectedByMip = 0u;
    var submergedAny = false;
    let waterSurface = sample_gpu_water_surface(pose, metadata[body]);
    for (var substep = 0u; substep < substeps; substep = substep + 1u) {
        let waterState = sample_water_state(
            pose, shape, metadata[body], waterSurface);
        let submerged = waterState.fraction;
        submergedAny = submergedAny || submerged > 1e-4;
        let buoyancyAcceleration = waterState.normal
            * length(sim.gravity_dt.xyz)
            * body_buoyancy(shapeType) * submerged;
        // Linear damping alone lets fast entries travel several body lengths
        // below their hydrostatic equilibrium before buoyancy can arrest
        // them. Add bounded quadratic drag in inverse-scale form; this stays
        // unconditionally stable at large substeps and vanishes smoothly at
        // rest, so it does not move the buoyancy equilibrium.
        let waterExtent = max(
            shape_vertical_extent(shape, pose.orientation), 0.1);
        let waterDrag = sim.water.w
            + 2.0 * length(motion.linearVelocity_sleep.xyz) / waterExtent;
        let waterLinearScale = 1.0 / (1.0 + waterDrag * submerged * dt);
        let waterAngularScale = 1.0 / (1.0 + sim.contact.x * submerged * dt);
        var velocities : BodyVelocities;
        velocities.linear = clamp_length(
            (motion.linearVelocity_sleep.xyz
             + (dryAcceleration + buoyancyAcceleration) * dt)
             * linearScale * waterLinearScale,
            sim.damping_clamps.z);
        velocities.angular = clamp_length(
            motion.angularVelocity_flags.xyz
             * angularScale * waterAngularScale,
            sim.damping_clamps.w);
        if (submerged > 1e-4 && inverseMass > 1e-7) {
            let buoyancyForce = buoyancyAcceleration / inverseMass;
            velocities.angular += inverse_inertia_world(
                pose.orientation, inverseInertia,
                cross(waterState.buoyancyCenter - pose.position_invMass.xyz,
                      buoyancyForce)) * dt;
            velocities.angular = clamp_length(
                velocities.angular, sim.damping_clamps.w);
        }

        let generated = generate_terrain_candidates(pose, shape);
        var contacts = reduce_terrain_candidates(generated);
        rejectedByMip = max(rejectedByMip, contacts.rejected);
        var accumulatedImpulses = vec4<f32>(0.0);
        for (var contactIndex = 0u; contactIndex < contacts.count;
             contactIndex += 1u) {
            let contact = contacts.items[contactIndex];
            let leverArm = contact.point - pose.position_invMass.xyz;
            var accumulated = 0.0;
            if (substep == 0u) {
                accumulated = 0.8 * cached_normal_impulse(
                    cache, contact.featureId);
                velocities = apply_body_impulse(
                    velocities, contact.normal * accumulated, leverArm,
                    inverseMass, inverseInertia, pose.orientation);
            }
            let pointVelocity = velocities.linear
                + cross(velocities.angular, leverArm);
            let normalVelocity = dot(pointVelocity, contact.normal);
            let penetrationBias = sim.solver.y
                * max(-contact.separation - sim.contact.w, 0.0) / dt;
            let restitutionVelocity = select(
                0.0, -terrain_restitution(body, shapeType) * normalVelocity,
                normalVelocity < -1.0);
            let effectiveMass = contact_effective_mass(
                leverArm, contact.normal, inverseMass, inverseInertia,
                pose.orientation);
            let updated = max(accumulated
                + effectiveMass * (penetrationBias + restitutionVelocity
                                 - normalVelocity), 0.0);
            velocities = apply_body_impulse(
                velocities, contact.normal * (updated - accumulated),
                leverArm, inverseMass, inverseInertia, pose.orientation);
            accumulatedImpulses[contactIndex] = updated;
        }

        for (var contactIndex = 0u; contactIndex < contacts.count;
             contactIndex += 1u) {
            let contact = contacts.items[contactIndex];
            let leverArm = contact.point - pose.position_invMass.xyz;
            let pointVelocity = velocities.linear
                + cross(velocities.angular, leverArm);
            let tangentVelocity = pointVelocity
                - contact.normal * dot(pointVelocity, contact.normal);
            let tangentSpeed = length(tangentVelocity);
            if (tangentSpeed > 1e-6) {
                let tangent = tangentVelocity / tangentSpeed;
                let tangentMass = contact_effective_mass(
                    leverArm, tangent, inverseMass, inverseInertia,
                    pose.orientation);
                let maximumFriction = terrain_friction(body)
                    * accumulatedImpulses[contactIndex];
                let frictionMagnitude = min(
                    tangentSpeed * tangentMass, maximumFriction);
                velocities = apply_body_impulse(
                    velocities, -tangent * frictionMagnitude, leverArm,
                    inverseMass, inverseInertia, pose.orientation);
            }
        }

        pose.position_invMass = vec4<f32>(
            pose.position_invMass.xyz + velocities.linear * dt,
            pose.position_invMass.w);
        let omega = vec4<f32>(velocities.angular, 0.0);
        pose.orientation += 0.5 * dt
            * quaternion_multiply(omega, pose.orientation);
        let qSquared = dot(pose.orientation, pose.orientation);
        if (qSquared > 1e-12) {
            pose.orientation *= inverseSqrt(qSquared);
        } else {
            pose.orientation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
        }

        var deepestPrediction = 0.0;
        var correctionNormal = vec3<f32>(0.0, 1.0, 0.0);
        for (var contactIndex = 0u; contactIndex < contacts.count;
             contactIndex += 1u) {
            let contact = contacts.items[contactIndex];
            let leverArm = contact.point - pose.position_invMass.xyz;
            let pointVelocity = velocities.linear
                + cross(velocities.angular, leverArm);
            let predicted = contact.separation
                + dot(pointVelocity, contact.normal) * dt;
            if (predicted < deepestPrediction) {
                deepestPrediction = predicted;
                correctionNormal = contact.normal;
            }
        }
        if (deepestPrediction < -sim.contact.w) {
            let correction = min(
                (-deepestPrediction - sim.contact.w) * 0.35, 0.20);
            pose.position_invMass = vec4<f32>(
                pose.position_invMass.xyz + correctionNormal * correction,
                pose.position_invMass.w);
        }

        for (var contactIndex = 0u; contactIndex < contacts.count;
             contactIndex += 1u) {
            let contact = contacts.items[contactIndex];
            let leverArm = contact.point - pose.position_invMass.xyz;
            let pointVelocity = velocities.linear
                + cross(velocities.angular, leverArm);
            let normalVelocity = dot(pointVelocity, contact.normal);
            if (normalVelocity < 0.0) {
                let effectiveMass = contact_effective_mass(
                    leverArm, contact.normal, inverseMass, inverseInertia,
                    pose.orientation);
                velocities = apply_body_impulse(
                    velocities, contact.normal * (-normalVelocity * effectiveMass),
                    leverArm, inverseMass, inverseInertia, pose.orientation);
            }
        }
        motion.linearVelocity_sleep = vec4<f32>(
            clamp_length(velocities.linear, sim.damping_clamps.z),
            motion.linearVelocity_sleep.w);
        motion.angularVelocity_flags = vec4<f32>(
            clamp_length(velocities.angular, sim.damping_clamps.w),
            motion.angularVelocity_flags.w);
        lastFeatures = vec4<u32>(0u);
        lastImpulses = vec4<f32>(0.0);
        for (var contactIndex = 0u; contactIndex < contacts.count;
             contactIndex += 1u) {
            lastFeatures[contactIndex] = contacts.items[contactIndex].featureId;
            lastImpulses[contactIndex] = accumulatedImpulses[contactIndex];
        }
        lastContactCount = contacts.count;
    }

    var worldData = metadata[body];
    var flags = body_flags(body)
        & ~(TERRAIN_CONTACT_MASK | TERRAIN_MIP_REJECTED | BODY_SUBMERGED);
    flags |= lastContactCount << TERRAIN_CONTACT_SHIFT;
    if (rejectedByMip != 0u) { flags |= TERRAIN_MIP_REJECTED; }
    if (submergedAny) { flags |= BODY_SUBMERGED; }
    let sleepThresholdSquared = sim.solver.z * sim.solver.z;
    let slow = dot(motion.linearVelocity_sleep.xyz,
                   motion.linearVelocity_sleep.xyz) < sleepThresholdSquared
        && dot(motion.angularVelocity_flags.xyz,
               motion.angularVelocity_flags.xyz) < sleepThresholdSquared;
    if (lastContactCount != 0u && slow && !submergedAny) {
        motion.linearVelocity_sleep.w += sim.gravity_dt.w;
        if (motion.linearVelocity_sleep.w >= sim.solver.w) {
            flags &= ~BODY_AWAKE;
            motion.linearVelocity_sleep = vec4<f32>(0.0);
            motion.angularVelocity_flags = vec4<f32>(
                vec3<f32>(0.0), f32(flags));
        }
    } else {
        motion.linearVelocity_sleep.w = 0.0;
    }
    normalize_world_position(&pose, &worldData);
    worldData.w = pack_generation_flags(u32(worldData.w), flags);
    metadata[body] = worldData;
    cache.normalImpulses = lastImpulses;
    cache.featureIds = lastFeatures;
    cache.state = vec4<u32>(u32(worldData.w) & GENERATION_MASK,
                            lastContactCount, 0u, 0u);
    terrainContactCaches[body] = cache;
    poses[body] = pose;
    motions[body] = motion;
    forces[body] = vec4<f32>(0.0);
}

// Production dynamic-world split. External forces, damping, and buoyancy are
// prepared here. The dynamic solver owns position integration; static terrain
// contacts are resolved by solve_static_contacts afterwards.
@compute @workgroup_size(256)
fn prepare_dynamic_bodies(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x >= atomicLoad(&counters[0])) { return; }
    let body = activeBodyIds[gid.x];
    if ((body_flags(body) & (BODY_ALIVE | BODY_AWAKE))
        != (BODY_ALIVE | BODY_AWAKE)) { return; }

    let pose = poses[body];
    var shape = shapes[body];
    let shapeType = u32(clamp(shape.dimensions_type.w, 0.0, 4.0));
    let inverseMass = pose.position_invMass.w;
    if (inverseMass <= 1e-7) {
        let flags = body_flags(body);
        let targetTick = atomicLoad(&counters[1]) + 1u;
        let hasKinematicTarget = (flags & BODY_KINEMATIC) != 0u
            && bitcast<u32>(motions[body].angularVelocity_flags.w)
                == targetTick;
        let sweepDistance = select(0.0,
            sim.gravity_dt.w * length(motions[body].linearVelocity_sleep.xyz),
            hasKinematicTarget);
        shape.dimensions_type.w = pack_shape_type_sweep(
            shapeType, sweepDistance);
        shapes[body] = shape;
        if (!hasKinematicTarget) {
            motions[body].linearVelocity_sleep = vec4<f32>(0.0);
            motions[body].angularVelocity_flags = vec4<f32>(0.0);
            if ((flags & BODY_KINEMATIC) != 0u) {
                set_body_flags(body, flags & ~BODY_AWAKE);
            }
        }
        forces[body] = vec4<f32>(0.0);
        return;
    }

    var motion = motions[body];
    let inverseInertia = shape_inverse_inertia(
        shape.dimensions_type.xyz, shapeType, inverseMass);
    let substeps = max(sim.counts.w, 1u);
    if (sim.water.y >= 0.5) {
        atomicAdd(&counters[6], water_sample_count(shapeType) * substeps);
    }
    let dt = sim.gravity_dt.w / f32(substeps);
    let linearScale = 1.0 / (1.0 + sim.damping_clamps.x * dt);
    let angularScale = 1.0 / (1.0 + sim.damping_clamps.y * dt);
    let dryAcceleration = sim.gravity_dt.xyz + forces[body].xyz * inverseMass;
    var submergedAny = false;
    let waterSurface = sample_gpu_water_surface(pose, metadata[body]);
    for (var substep = 0u; substep < substeps; substep += 1u) {
        let waterState = sample_water_state(
            pose, shape, metadata[body], waterSurface);
        let submerged = waterState.fraction;
        submergedAny = submergedAny || submerged > 1e-4;
        let buoyancyAcceleration = waterState.normal
            * length(sim.gravity_dt.xyz)
            * body_buoyancy(shapeType) * submerged;
        let waterExtent = max(
            shape_vertical_extent(shape, pose.orientation), 0.1);
        let waterDrag = sim.water.w
            + 2.0 * length(motion.linearVelocity_sleep.xyz) / waterExtent;
        let waterLinearScale = 1.0 / (1.0 + waterDrag * submerged * dt);
        let waterAngularScale = 1.0 / (1.0 + sim.contact.x * submerged * dt);
        var linear = clamp_length(
            (motion.linearVelocity_sleep.xyz
             + (dryAcceleration + buoyancyAcceleration) * dt)
             * linearScale * waterLinearScale,
            sim.damping_clamps.z);
        var angular = clamp_length(
            motion.angularVelocity_flags.xyz
             * angularScale * waterAngularScale,
            sim.damping_clamps.w);
        if (submerged > 1e-4) {
            let buoyancyForce = buoyancyAcceleration / inverseMass;
            angular += inverse_inertia_world(
                pose.orientation, inverseInertia,
                cross(waterState.buoyancyCenter - pose.position_invMass.xyz,
                      buoyancyForce)) * dt;
            angular = clamp_length(angular, sim.damping_clamps.w);
        }
        motion.linearVelocity_sleep = vec4<f32>(linear,
            motion.linearVelocity_sleep.w);
        motion.angularVelocity_flags = vec4<f32>(angular,
            motion.angularVelocity_flags.w);
    }
    var flags = body_flags(body) & ~BODY_SUBMERGED;
    if (submergedAny) {
        flags |= BODY_SUBMERGED;
        atomicAdd(&counters[5], 1u);
    }
    let linearSweep = sim.gravity_dt.w
        * length(motion.linearVelocity_sleep.xyz);
    let dimensions = abs(shape.dimensions_type.xyz);
    let minimumThickness = max(min(dimensions.x,
        min(dimensions.y, dimensions.z)), 1e-4);
    // If every body moves less than half its own minimum thickness, a pair
    // cannot exchange sides during this tick: their combined travel is no
    // greater than their combined minimum support radii. Keep ordinary pile
    // bodies on the exact discrete path and pay the broader search only for a
    // body that can actually tunnel.
    let sweepDistance = select(0.0, linearSweep,
        linearSweep >= 0.5 * minimumThickness);
    shape.dimensions_type.w = pack_shape_type_sweep(
        shapeType, sweepDistance);
    shapes[body] = shape;
    set_body_flags(body, flags);
    motions[body] = motion;
    forces[body] = vec4<f32>(0.0);
}

@compute @workgroup_size(128)
fn solve_static_contacts(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x >= atomicLoad(&counters[0])) { return; }
    let body = activeBodyIds[gid.x];
    if ((body_flags(body) & (BODY_ALIVE | BODY_AWAKE))
        != (BODY_ALIVE | BODY_AWAKE)) { return; }

    var pose = poses[body];
    var motion = motions[body];
    var linearActivity = motion.linearVelocity_sleep.xyz;
    let shape = shapes[body];
    let inverseMass = pose.position_invMass.w;
    if (inverseMass <= 1e-7) { return; }
    let shapeType = u32(clamp(shape.dimensions_type.w, 0.0, 4.0));
    let inverseInertia = shape_inverse_inertia(
        shape.dimensions_type.xyz, shapeType, inverseMass);
    var cache = terrainContactCaches[body];
    if (cache.state.x != metadata_generation(body)) {
        cache.normalImpulses = vec4<f32>(0.0);
        cache.featureIds = vec4<u32>(0u);
        cache.state = vec4<u32>(metadata_generation(body), 0u, 0u, 0u);
    }

    let worldDataBeforeSolve = metadata[body];
    var terrainFrameValid = false;
    let terrainPose = pose_in_world_frame(
        pose, worldDataBeforeSolve,
        terrain_horizontal_sector_radius(shape),
        terrain_vertical_sector_radius(shape), &terrainFrameValid);
    var generated : TerrainCandidateSet;
    generated.count = 0u;
    generated.rejected = select(
        0u, 1u,
        !terrainFrameValid && sim.terrainSize_mips_flags.w != 0u);
    if (terrainFrameValid) {
        generated = generate_terrain_candidates(terrainPose, shape);
    }
    var contacts = reduce_terrain_candidates(generated);
    if (contacts.count != 0u) {
        atomicAdd(&counters[2], 1u);
        atomicAdd(&counters[3], contacts.count);
        atomicMax(&counters[4], contacts.count);
    }
    let dt = sim.gravity_dt.w / f32(max(sim.counts.w, 1u));
    var velocities : BodyVelocities;
    velocities.linear = motion.linearVelocity_sleep.xyz;
    velocities.angular = motion.angularVelocity_flags.xyz;
    var accumulatedImpulses = vec4<f32>(0.0);
    for (var contactIndex = 0u; contactIndex < contacts.count;
         contactIndex += 1u) {
        let contact = contacts.items[contactIndex];
        let leverArm = contact.point - terrainPose.position_invMass.xyz;
        var accumulated = 0.8 * cached_normal_impulse(
            cache, contact.featureId);
        velocities = apply_body_impulse(
            velocities, contact.normal * accumulated, leverArm,
            inverseMass, inverseInertia, pose.orientation);
        let pointVelocity = velocities.linear
            + cross(velocities.angular, leverArm);
        let normalVelocity = dot(pointVelocity, contact.normal);
        let penetrationBias = sim.solver.y
            * max(-contact.separation - sim.contact.w, 0.0) / max(dt, 1e-7);
        let restitutionVelocity = select(
            0.0, -terrain_restitution(body, shapeType) * normalVelocity,
            normalVelocity < -1.0);
        let effectiveMass = contact_effective_mass(
            leverArm, contact.normal, inverseMass, inverseInertia,
            pose.orientation);
        let updated = max(accumulated
            + effectiveMass * (penetrationBias + restitutionVelocity
                             - normalVelocity), 0.0);
        velocities = apply_body_impulse(
            velocities, contact.normal * (updated - accumulated), leverArm,
            inverseMass, inverseInertia, pose.orientation);
        accumulatedImpulses[contactIndex] = updated;
    }

    var supportNormalSum = vec3<f32>(0.0);
    var supportImpulse = 0.0;
    var supportRadius = sim.contact.w;
    for (var contactIndex = 0u; contactIndex < contacts.count;
         contactIndex += 1u) {
        let contact = contacts.items[contactIndex];
        let leverArm = contact.point - terrainPose.position_invMass.xyz;
        let normalImpulse = accumulatedImpulses[contactIndex];
        supportNormalSum += contact.normal * normalImpulse;
        supportImpulse += normalImpulse;
        supportRadius = max(supportRadius, length(leverArm));
        let pointVelocity = velocities.linear
            + cross(velocities.angular, leverArm);
        let tangentVelocity = pointVelocity
            - contact.normal * dot(pointVelocity, contact.normal);
        let tangentSpeed = length(tangentVelocity);
        if (tangentSpeed > 1e-6) {
            let tangent = tangentVelocity / tangentSpeed;
            let tangentMass = contact_effective_mass(
                leverArm, tangent, inverseMass, inverseInertia,
                pose.orientation);
            let maximumFriction = terrain_friction(body)
                * normalImpulse;
            let frictionMagnitude = min(
                tangentSpeed * tangentMass, maximumFriction);
            velocities = apply_body_impulse(
                velocities, -tangent * frictionMagnitude, leverArm,
                inverseMass, inverseInertia, pose.orientation);
        }
    }

    if (supportImpulse > 1e-7) {
        let normalLength = length(supportNormalSum);
        if (normalLength > 1e-7) {
            let supportNormal = supportNormalSum / normalLength;
            let supportSpeed = dot(linearActivity, supportNormal);
            if (supportSpeed < 0.0) {
                linearActivity -= supportNormal * supportSpeed;
            }
            let twistSpeed = dot(velocities.angular, supportNormal);
            let twistInverseMass = dot(supportNormal,
                inverse_inertia_world(
                    pose.orientation, inverseInertia, supportNormal));
            if (twistInverseMass > 1e-7) {
                let maximumTwistImpulse = terrain_friction(body)
                    * supportImpulse * supportRadius;
                let twistImpulse = clamp(
                    -twistSpeed / twistInverseMass,
                    -maximumTwistImpulse, maximumTwistImpulse);
                velocities.angular += inverse_inertia_world(
                    pose.orientation, inverseInertia,
                    supportNormal * twistImpulse);
            }

            let rollingVelocity = velocities.angular
                - supportNormal
                    * dot(velocities.angular, supportNormal);
            let rollingSpeed = length(rollingVelocity);
            if (rollingSpeed > 1e-7) {
                let rollingDirection = rollingVelocity / rollingSpeed;
                let rollingInverseMass = dot(rollingDirection,
                    inverse_inertia_world(
                        pose.orientation, inverseInertia,
                        rollingDirection));
                if (rollingInverseMass > 1e-7) {
                    let rollingImpulse = min(
                        rollingSpeed / rollingInverseMass,
                        terrain_rolling_resistance(body) * supportImpulse);
                    velocities.angular += inverse_inertia_world(
                        pose.orientation, inverseInertia,
                        -rollingDirection * rollingImpulse);
                }
            }
        }
    }

    var deepest = 0.0;
    var correctionNormal = vec3<f32>(0.0, 1.0, 0.0);
    for (var contactIndex = 0u; contactIndex < contacts.count;
         contactIndex += 1u) {
        if (contacts.items[contactIndex].separation < deepest) {
            deepest = contacts.items[contactIndex].separation;
            correctionNormal = contacts.items[contactIndex].normal;
        }
    }
    if (deepest < -sim.contact.w) {
        let correction = min((-deepest - sim.contact.w) * 0.35, 0.20);
        pose.position_invMass = vec4<f32>(
            pose.position_invMass.xyz + correctionNormal * correction,
            pose.position_invMass.w);
    }

    motion.linearVelocity_sleep = vec4<f32>(
        clamp_length(velocities.linear, sim.damping_clamps.z),
        // Terrain support cancels inward motion before it reaches the next
        // rendered pose. Preserve only motion that can visibly translate it.
        dot(linearActivity, linearActivity));
    motion.angularVelocity_flags = vec4<f32>(
        clamp_length(velocities.angular, sim.damping_clamps.w),
        motion.angularVelocity_flags.w);
    var worldData = metadata[body];
    var flags = body_flags(body)
        & ~(TERRAIN_CONTACT_MASK | TERRAIN_MIP_REJECTED);
    flags |= contacts.count << TERRAIN_CONTACT_SHIFT;
    if (contacts.rejected != 0u) { flags |= TERRAIN_MIP_REJECTED; }
    normalize_world_position(&pose, &worldData);
    worldData.w = pack_generation_flags(u32(worldData.w), flags);
    metadata[body] = worldData;
    cache.normalImpulses = accumulatedImpulses;
    cache.featureIds = vec4<u32>(0u);
    for (var contactIndex = 0u; contactIndex < contacts.count;
         contactIndex += 1u) {
        cache.featureIds[contactIndex] = contacts.items[contactIndex].featureId;
    }
    cache.state = vec4<u32>(u32(worldData.w) & GENERATION_MASK,
                            contacts.count, 0u, 0u);
    terrainContactCaches[body] = cache;
    poses[body] = pose;
    motions[body] = motion;
}

@compute @workgroup_size(1)
fn advance_tick(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    atomicMax(&counters[9], atomicLoad(&counters[2]));
    atomicMax(&counters[10], atomicLoad(&counters[3]));
    atomicMax(&counters[11], atomicLoad(&counters[5]));
    atomicMax(&counters[12], atomicLoad(&counters[6]));
    atomicAdd(&counters[1], 1u);
}

@compute @workgroup_size(256)
fn pack_debug(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x >= sim.debugRange.y) { return; }
    let body = sim.debugRange.x + gid.x;
    if (body >= sim.counts.x) { return; }
    let output = gid.x * 9u;
    debugPacked[output + 0u] = bitcast<vec4<u32>>(poses[body].position_invMass);
    debugPacked[output + 1u] = bitcast<vec4<u32>>(poses[body].orientation);
    debugPacked[output + 2u] = bitcast<vec4<u32>>(motions[body].linearVelocity_sleep);
    debugPacked[output + 3u] = bitcast<vec4<u32>>(motions[body].angularVelocity_flags);
    debugPacked[output + 4u] = bitcast<vec4<u32>>(shapes[body].dimensions_type);
    debugPacked[output + 5u] = bitcast<vec4<u32>>(shapes[body].invInertia_material);
    debugPacked[output + 6u] =
        bitcast<vec4<u32>>(shapes[body].material_coefficients);
    debugPacked[output + 7u] = vec4<u32>(
        bitcast<u32>(shapes[body].invInertia_material.w), 0u, 0u, 0u);
    debugPacked[output + 8u] = bitcast<vec4<u32>>(metadata[body]);
}
