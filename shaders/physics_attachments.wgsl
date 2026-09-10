const GENERATION_MASK : u32 = 0x000fffffu;
const BODY_ALIVE : u32 = 1u << 20u;
const BODY_AWAKE : u32 = 1u << 21u;

const ATTACHMENT_ALIVE : u32 = 1u;
const ATTACHMENT_BROKEN : u32 = 2u;

const COMMAND_DESTROY : u32 = 0u;
const COMMAND_CREATE_DISTANCE : u32 = 1u;
const COMMAND_SET_TARGET_LENGTH : u32 = 2u;
const COMMAND_SET_MOTOR_SPEED : u32 = 3u;

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

struct AttachmentCommand {
    header : vec4<u32>,
    bodies : vec4<u32>,
    anchorATarget : vec4<f32>,
    anchorBMotor : vec4<f32>,
    limits : vec4<f32>,
    material : vec4<f32>,
};

struct DistanceAttachment {
    identity : vec4<u32>,
    bodyGenerations : vec4<u32>,
    anchorATarget : vec4<f32>,
    anchorBMotor : vec4<f32>,
    limits : vec4<f32>,
    evidence : vec4<f32>,
    breakContext : vec4<u32>,
    reserved : vec4<u32>,
};

struct AttachmentParams {
    counts : vec4<u32>,
    tuning : vec4<f32>,
    world : vec4<u32>,
};

@group(0) @binding(0) var<storage, read_write> poses : array<BodyPose>;
@group(0) @binding(1) var<storage, read_write> motions : array<BodyMotion>;
@group(0) @binding(2) var<storage, read> shapes : array<BodyShape>;
@group(0) @binding(3) var<storage, read_write> metadata : array<vec4<i32>>;
@group(0) @binding(4) var<storage, read_write> coreCounters : array<atomic<u32>>;
@group(0) @binding(5) var<storage, read_write> attachments : array<DistanceAttachment>;
@group(0) @binding(6) var<storage, read> commands : array<AttachmentCommand>;
@group(0) @binding(7) var<storage, read_write> telemetry : array<atomic<u32>>;
@group(0) @binding(8) var<uniform> params : AttachmentParams;

fn quaternion_rotate(q : vec4<f32>, value : vec3<f32>) -> vec3<f32> {
    let twiceCross = 2.0 * cross(q.xyz, value);
    return value + q.w * twiceCross + cross(q.xyz, twiceCross);
}

fn quaternion_inverse_rotate(q : vec4<f32>, value : vec3<f32>) -> vec3<f32> {
    return quaternion_rotate(vec4<f32>(-q.xyz, q.w), value);
}

fn inverse_inertia(body : u32, torque : vec3<f32>) -> vec3<f32> {
    let orientation = poses[body].orientation;
    let local = quaternion_inverse_rotate(orientation, torque);
    return quaternion_rotate(
        orientation, local * shapes[body].invInertia_material.xyz);
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

fn endpoint_is_live(body : u32, generation : u32) -> bool {
    if (body == 0u || body >= params.counts.z) { return false; }
    let flags = u32(metadata[body].w);
    return (flags & BODY_ALIVE) != 0u
        && (flags & GENERATION_MASK) == (generation & GENERATION_MASK);
}

fn clear_current_telemetry(tick : u32) {
    for (var index = 0u; index < 7u; index += 1u) {
        atomicStore(&telemetry[index], 0u);
    }
    atomicStore(&telemetry[7], tick);
}

fn count_stale_command() {
    atomicAdd(&telemetry[5], 1u);
}

fn apply_commands(tick : u32) {
    for (var rank = 0u; rank < params.counts.y; rank += 1u) {
        let command = commands[rank];
        if (command.header.w != tick) { continue; }
        atomicAdd(&telemetry[4], 1u);
        let commandType = command.header.x;
        let slot = command.header.y;
        let generation = command.header.z & GENERATION_MASK;
        if (slot == 0u || slot >= params.counts.x) {
            count_stale_command();
            continue;
        }
        var attachment = attachments[slot];
        if (commandType == COMMAND_CREATE_DISTANCE) {
            attachment.identity = vec4<u32>(
                generation, ATTACHMENT_ALIVE,
                command.bodies.x, command.bodies.z);
            attachment.bodyGenerations = vec4<u32>(
                command.bodies.y & GENERATION_MASK,
                command.bodies.w & GENERATION_MASK, 0u, 0u);
            attachment.anchorATarget = command.anchorATarget;
            attachment.anchorBMotor = command.anchorBMotor;
            attachment.limits = command.limits;
            attachment.evidence = vec4<f32>(0.0);
            attachment.breakContext = vec4<u32>(0u);
            attachment.reserved = vec4<u32>(0u,bitcast<u32>(command.material.x),0u,0u);
            attachments[slot] = attachment;
            continue;
        }
        if ((attachment.identity.x & GENERATION_MASK) != generation) {
            count_stale_command();
            continue;
        }
        if (commandType == COMMAND_DESTROY) {
            attachments[slot] = DistanceAttachment(
                vec4<u32>(0u), vec4<u32>(0u),
                vec4<f32>(0.0), vec4<f32>(0.0),
                vec4<f32>(0.0), vec4<f32>(0.0),
                vec4<u32>(0u), vec4<u32>(0u));
        } else if ((attachment.identity.y & ATTACHMENT_ALIVE) == 0u) {
            count_stale_command();
        } else if (commandType == COMMAND_SET_TARGET_LENGTH) {
            attachment.anchorATarget.w = clamp(
                command.anchorATarget.w,
                attachment.limits.x, attachment.limits.y);
            attachments[slot] = attachment;
        } else if (commandType == COMMAND_SET_MOTOR_SPEED) {
            attachment.anchorBMotor.w = command.anchorBMotor.w;
            attachments[slot] = attachment;
        } else {
            count_stale_command();
        }
    }
}

fn wake_body(body : u32) {
    var worldMeta = metadata[body];
    worldMeta.w = i32(u32(worldMeta.w) | BODY_AWAKE);
    metadata[body] = worldMeta;
    var motion = motions[body];
    motion.linearVelocity_sleep.w = 0.0;
    motions[body] = motion;
}

fn solve_attachment(slot : u32, tick : u32, iteration : u32) {
    var attachment = attachments[slot];
    if ((attachment.identity.y & ATTACHMENT_ALIVE) == 0u) { return; }
    let bodyA = attachment.identity.z;
    let bodyB = attachment.identity.w;
    if (!endpoint_is_live(bodyA, attachment.bodyGenerations.x)
        || !endpoint_is_live(bodyB, attachment.bodyGenerations.y)) {
        atomicAdd(&telemetry[6], 1u);
        attachment.identity.y = 0u;
        attachments[slot] = attachment;
        return;
    }

    let previousTarget=attachment.anchorATarget.w;
    if(iteration==0u) {
        attachment.reserved.x=0u; // Accumulated tension impulse for this tick.
        attachment.anchorATarget.w = clamp(
            attachment.anchorATarget.w
                - attachment.anchorBMotor.w * params.tuning.x,
            attachment.limits.x, attachment.limits.y);
    }

    var sectorDelta = vec3<i32>(0);
    for (var axis = 0u; axis < 3u; axis += 1u) {
        sectorDelta[axis] = bounded_sector_delta(
            metadata[bodyA][axis], metadata[bodyB][axis], params.world.x);
        if (sectorDelta[axis] == 2147483647) {
            atomicAdd(&telemetry[6], 1u);
            attachment.identity.y = 0u;
            attachments[slot] = attachment;
            return;
        }
    }

    let leverA = quaternion_rotate(
        poses[bodyA].orientation, attachment.anchorATarget.xyz);
    let leverB = quaternion_rotate(
        poses[bodyB].orientation, attachment.anchorBMotor.xyz);
    let endpointA = poses[bodyA].position_invMass.xyz + leverA;
    let endpointB = poses[bodyB].position_invMass.xyz + leverB
        + vec3<f32>(sectorDelta) * 256.0;
    let delta = endpointB - endpointA;
    let distanceSquared = dot(delta, delta);
    let distance = sqrt(max(distanceSquared, 0.0));
    attachment.evidence = vec4<f32>(
        0.0, 0.0, distance, attachment.anchorATarget.w);
    if(iteration==0u) { atomicAdd(&telemetry[0], 1u); }

    var extension = distance - attachment.anchorATarget.w;
    if (distance <= 1e-7 || extension <= params.tuning.y) {
        if(iteration==0u) { atomicAdd(&telemetry[2], 1u); }
        attachments[slot] = attachment;
        return;
    }
    if(iteration==0u) { atomicAdd(&telemetry[1], 1u); }

    let direction = delta / distance;
    let motionA = motions[bodyA];
    let motionB = motions[bodyB];
    let pointVelocityA = motionA.linearVelocity_sleep.xyz
        + cross(motionA.angularVelocity_flags.xyz, leverA);
    let pointVelocityB = motionB.linearVelocity_sleep.xyz
        + cross(motionB.angularVelocity_flags.xyz, leverB);
    let relativeSpeed = dot(pointVelocityB - pointVelocityA, direction);
    let angularA = inverse_inertia(bodyA, cross(leverA, direction));
    let angularB = inverse_inertia(bodyB, cross(leverB, direction));
    let inverseEffectiveMass =
        poses[bodyA].position_invMass.w + poses[bodyB].position_invMass.w
        + dot(cross(angularA, leverA) + cross(angularB, leverB),
              direction);
    if (inverseEffectiveMass <= 1e-9) {
        if(attachment.anchorBMotor.w>0.0) { attachment.anchorATarget.w=previousTarget; }
        attachments[slot] = attachment;
        return;
    }

    // Optional axial elasticity. Backward-Euler spring/damper coefficients
    // use the actual endpoint effective mass. Critical damping absorbs snatch
    // energy; static extension remains force * compliance. A zero material
    // value takes the original hard-rope path exactly.
    var correctionFactor=params.tuning.z;
    var softness=0.0;
    let compliance=bitcast<f32>(attachment.reserved.y);
    if(compliance>0.0) {
        let stiffness=1.0/compliance;
        let damping=2.0*sqrt(stiffness/inverseEffectiveMass);
        let denominator=damping+params.tuning.x*stiffness;
        correctionFactor=params.tuning.x*stiffness/denominator;
        softness=1.0/(params.tuning.x*denominator);
    }

    // A force-limited winch cannot wind in an immovable length of cable. Limit
    // this tick's take-up to the extension supportable by its motor impulse.
    // Never pay out the previously held length here: external separating
    // motion/impacts still create real overload and can break the rope.
    if(iteration==0u && attachment.anchorBMotor.w>0.0 && correctionFactor>0.0) {
        let supportedExtension=max((attachment.limits.z*params.tuning.x*(inverseEffectiveMass+softness)-relativeSpeed)
            *params.tuning.x/correctionFactor,0.0);
        attachment.anchorATarget.w=max(attachment.anchorATarget.w,min(previousTarget,distance-supportedExtension));
        extension=max(distance-attachment.anchorATarget.w,0.0);
        attachment.evidence.w=attachment.anchorATarget.w;
    }
    let correctionSpeed = extension * correctionFactor / params.tuning.x;
    // Revisit shared bodies with accumulated sequential impulses. A later
    // line can undo an earlier line's correction; applying only positive
    // increments would pump energy into a suspended multirope load.
    let previousImpulse=bitcast<f32>(attachment.reserved.x);
    let requiredImpulse = max(previousImpulse+
        (relativeSpeed + correctionSpeed-softness*previousImpulse) / (inverseEffectiveMass+softness), 0.0);
    let requiredForce = requiredImpulse / params.tuning.x;
    attachment.evidence.x = requiredImpulse;
    attachment.evidence.y = requiredForce;
    let breakForce = attachment.limits.w;
    let broke=iteration==7u && breakForce > 0.0 && requiredForce > breakForce;
    if (broke) {
        attachment.identity.y = ATTACHMENT_BROKEN;
        attachment.breakContext = vec4<u32>(tick, 1u, 0u, 0u);
        atomicAdd(&telemetry[3], 1u);
    }

    // A broken line contributes no impulse to this tick's integration. Undo
    // its earlier solver iterations while retaining overload evidence.
    let appliedImpulse = select(min(
        requiredImpulse, attachment.limits.z * params.tuning.x),0.0,broke);
    attachment.reserved.x=bitcast<u32>(appliedImpulse);
    let deltaImpulse=appliedImpulse-previousImpulse;
    if (abs(deltaImpulse) > 0.0) {
        let impulse = direction * deltaImpulse;
        var updatedA = motionA;
        var updatedB = motionB;
        updatedA.linearVelocity_sleep = vec4<f32>(
            updatedA.linearVelocity_sleep.xyz
                + impulse * poses[bodyA].position_invMass.w,
            0.0);
        updatedA.angularVelocity_flags = vec4<f32>(
            updatedA.angularVelocity_flags.xyz
                + inverse_inertia(bodyA, cross(leverA, impulse)),
            updatedA.angularVelocity_flags.w);
        updatedB.linearVelocity_sleep = vec4<f32>(
            updatedB.linearVelocity_sleep.xyz
                - impulse * poses[bodyB].position_invMass.w,
            0.0);
        updatedB.angularVelocity_flags = vec4<f32>(
            updatedB.angularVelocity_flags.xyz
                - inverse_inertia(bodyB, cross(leverB, impulse)),
            updatedB.angularVelocity_flags.w);
        motions[bodyA] = updatedA;
        motions[bodyB] = updatedB;
        wake_body(bodyA);
        wake_body(bodyB);
    }
    attachments[slot] = attachment;
}

@compute @workgroup_size(1)
fn solve_attachments(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    let tick = atomicLoad(&coreCounters[1]) + 1u;
    clear_current_telemetry(tick);
    apply_commands(tick);
    // One bounded serial island sweep preserves race-free writes for shared
    // endpoints. Motors advance once; force caps apply to the total impulse,
    // never eight independent impulses. General parallel island work is later.
    for(var iteration=0u;iteration<8u;iteration+=1u) {
        for (var slot = 1u; slot < params.counts.x; slot += 1u) {
            solve_attachment(slot, tick, iteration);
        }
    }
    atomicMax(&telemetry[8], atomicLoad(&telemetry[0]));
    atomicMax(&telemetry[9], atomicLoad(&telemetry[1]));
    atomicMax(&telemetry[10], atomicLoad(&telemetry[2]));
    atomicMax(&telemetry[11], atomicLoad(&telemetry[3]));
    atomicMax(&telemetry[12], atomicLoad(&telemetry[4]));
}
