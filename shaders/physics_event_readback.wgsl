const CONTACT_BEGIN : u32 = 1u;
const CONTACT_END : u32 = 2u;
const EVENT_CONTACT_HIT : u32 = 3u;
const ISLAND_SLEEP : u32 = 1u;
const ISLAND_WAKE : u32 = 2u;
const EVENT_CONTACT_BEGIN : u32 = 1u;
const EVENT_CONTACT_END : u32 = 2u;
const EVENT_ISLAND_SLEEP : u32 = 4u;
const EVENT_ISLAND_WAKE : u32 = 5u;
const EVENT_ATTACHMENT_BREAK : u32 = 6u;
const ATTACHMENT_BROKEN : u32 = 2u;
const SENTINEL : u32 = 0xffffffffu;
const GENERATION_MASK : u32 = 0x000fffffu;

struct ContactEvent {
    pairLow : u32,
    pairHigh : u32,
    eventType : u32,
    contactId : u32,
    identity : vec4<u32>,
};

struct IslandEvent {
    rootBody : u32,
    eventType : u32,
    tick : u32,
    bodyCount : u32,
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

struct EventParams {
    counts : vec4<u32>,
    tick : vec4<u32>,
    attachments : vec4<u32>,
};

@group(0) @binding(0) var<storage, read> contactEvents : array<ContactEvent>;
@group(0) @binding(1) var<storage, read> sourceTelemetry : array<u32>;
@group(0) @binding(2) var<storage, read> islandEvents : array<IslandEvent>;
@group(0) @binding(4) var<storage, read_write> packet : array<vec4<u32>>;
@group(0) @binding(5) var<storage, read> manifolds : array<ContactManifold>;
@group(0) @binding(7) var<uniform> params : EventParams;
@group(0) @binding(8) var<storage, read> metadata : array<vec4<i32>>;
@group(0) @binding(9) var<storage, read> attachments : array<DistanceAttachment>;

fn body_generation(body : u32) -> u32 {
    if (body == SENTINEL || body >= params.tick.w) { return 0u; }
    return u32(metadata[body].w) & GENERATION_MASK;
}

fn append_event(count : ptr<function, u32>, overflow : ptr<function, u32>,
                eventType : u32, bodyA : u32, bodyB : u32,
                generationA : u32, generationB : u32,
                feature : u32, sourceId : u32, auxiliary : u32,
                flags : u32, localAnchorASeparation : vec4<f32>,
                localAnchorBImpulse : vec4<f32>,
                normalSpeed : vec4<f32>) {
    let output = *count;
    if (output < params.counts.z) {
        let word = 1u + output * 6u;
        packet[word] = vec4<u32>(params.tick.x, eventType, bodyA, bodyB);
        packet[word + 1u] = vec4<u32>(
            feature, sourceId, auxiliary, flags);
        packet[word + 2u] = vec4<u32>(
            generationA, generationB, 0u, 0u);
        packet[word + 3u] = bitcast<vec4<u32>>(
            localAnchorASeparation);
        packet[word + 4u] = bitcast<vec4<u32>>(
            localAnchorBImpulse);
        packet[word + 5u] = bitcast<vec4<u32>>(normalSpeed);
    } else {
        *overflow = 1u;
    }
    *count = output + 1u;
}

@compute @workgroup_size(1)
fn pack_events(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    var count = 0u;
    var overflow = 0u;
    if ((params.counts.w & 1u) != 0u) {
        let beginCount = min(sourceTelemetry[0], params.counts.x);
        let endCount = min(sourceTelemetry[1], params.counts.x);
        for (var index = 0u; index < beginCount; index += 1u) {
            let event = contactEvents[index];
            if (event.eventType == CONTACT_BEGIN) {
                append_event(&count, &overflow, EVENT_CONTACT_BEGIN,
                    event.pairLow, event.pairHigh,
                    event.identity.x, event.identity.y,
                    0u, event.contactId, 0u, 0u,
                    vec4<f32>(0.0), vec4<f32>(0.0),
                    vec4<f32>(0.0));
            }
        }
        for (var index = 0u; index < endCount; index += 1u) {
            let event = contactEvents[params.counts.x + index];
            if (event.eventType == CONTACT_END) {
                append_event(&count, &overflow, EVENT_CONTACT_END,
                    event.pairLow, event.pairHigh,
                    event.identity.x, event.identity.y,
                    0u, event.contactId, 0u, 0u,
                    vec4<f32>(0.0), vec4<f32>(0.0),
                    vec4<f32>(0.0));
            }
        }
        overflow |= select(0u, 1u, sourceTelemetry[2] != 0u);
    }
    if ((params.counts.w & 4u) != 0u) {
        let manifoldCount = min(sourceTelemetry[5], params.tick.z);
        for (var index = 0u; index < manifoldCount; index += 1u) {
            let manifold = manifolds[index];
            var impactSpeed = 0.0;
            var normalImpulse = 0.0;
            var weightedSourceAnchorA = vec3<f32>(0.0);
            var weightedSourceAnchorB = vec3<f32>(0.0);
            var weightedSeparation = 0.0;
            var strongestImpulse = -1.0;
            var strongestSourceFeatureA = 0u;
            var strongestSourceFeatureB = 0u;
            if (manifold.state.x > 0u) {
                impactSpeed = max(impactSpeed, manifold.points[0].impulses.y);
                let pointImpulse =
                    manifold.points[0].localAnchorB_normalImpulse.w;
                normalImpulse += pointImpulse;
                weightedSourceAnchorA +=
                    manifold.points[0].localAnchorA_separation.xyz
                    * pointImpulse;
                weightedSourceAnchorB +=
                    manifold.points[0].localAnchorB_normalImpulse.xyz
                    * pointImpulse;
                weightedSeparation +=
                    manifold.points[0].localAnchorA_separation.w
                    * pointImpulse;
                if (pointImpulse > strongestImpulse) {
                    strongestImpulse = pointImpulse;
                    strongestSourceFeatureA =
                        manifold.points[0].features.x;
                    strongestSourceFeatureB =
                        manifold.points[0].features.y;
                }
            }
            if (manifold.state.x > 1u) {
                impactSpeed = max(impactSpeed, manifold.points[1].impulses.y);
                let pointImpulse =
                    manifold.points[1].localAnchorB_normalImpulse.w;
                normalImpulse += pointImpulse;
                weightedSourceAnchorA +=
                    manifold.points[1].localAnchorA_separation.xyz
                    * pointImpulse;
                weightedSourceAnchorB +=
                    manifold.points[1].localAnchorB_normalImpulse.xyz
                    * pointImpulse;
                weightedSeparation +=
                    manifold.points[1].localAnchorA_separation.w
                    * pointImpulse;
                if (pointImpulse > strongestImpulse) {
                    strongestImpulse = pointImpulse;
                    strongestSourceFeatureA =
                        manifold.points[1].features.x;
                    strongestSourceFeatureB =
                        manifold.points[1].features.y;
                }
            }
            if (manifold.state.x > 2u) {
                impactSpeed = max(impactSpeed, manifold.points[2].impulses.y);
                let pointImpulse =
                    manifold.points[2].localAnchorB_normalImpulse.w;
                normalImpulse += pointImpulse;
                weightedSourceAnchorA +=
                    manifold.points[2].localAnchorA_separation.xyz
                    * pointImpulse;
                weightedSourceAnchorB +=
                    manifold.points[2].localAnchorB_normalImpulse.xyz
                    * pointImpulse;
                weightedSeparation +=
                    manifold.points[2].localAnchorA_separation.w
                    * pointImpulse;
                if (pointImpulse > strongestImpulse) {
                    strongestImpulse = pointImpulse;
                    strongestSourceFeatureA =
                        manifold.points[2].features.x;
                    strongestSourceFeatureB =
                        manifold.points[2].features.y;
                }
            }
            if (manifold.state.x > 3u) {
                impactSpeed = max(impactSpeed, manifold.points[3].impulses.y);
                let pointImpulse =
                    manifold.points[3].localAnchorB_normalImpulse.w;
                normalImpulse += pointImpulse;
                weightedSourceAnchorA +=
                    manifold.points[3].localAnchorA_separation.xyz
                    * pointImpulse;
                weightedSourceAnchorB +=
                    manifold.points[3].localAnchorB_normalImpulse.xyz
                    * pointImpulse;
                weightedSeparation +=
                    manifold.points[3].localAnchorA_separation.w
                    * pointImpulse;
                if (pointImpulse > strongestImpulse) {
                    strongestImpulse = pointImpulse;
                    strongestSourceFeatureA =
                        manifold.points[3].features.x;
                    strongestSourceFeatureB =
                        manifold.points[3].features.y;
                }
            }
            if (impactSpeed >= 1.0 && normalImpulse > 0.0) {
                let inverseImpulse = 1.0 / normalImpulse;
                let sourceASorted =
                    manifold.pair.keyHigh <= manifold.pair.keyLow;
                let canonicalBodyA = select(
                    manifold.pair.keyLow, manifold.pair.keyHigh,
                    sourceASorted);
                let canonicalBodyB = select(
                    manifold.pair.keyHigh, manifold.pair.keyLow,
                    sourceASorted);
                let canonicalAnchorA = select(
                    weightedSourceAnchorB, weightedSourceAnchorA,
                    sourceASorted) * inverseImpulse;
                let canonicalAnchorB = select(
                    weightedSourceAnchorA, weightedSourceAnchorB,
                    sourceASorted) * inverseImpulse;
                let canonicalFeatureA = select(
                    strongestSourceFeatureB,
                    strongestSourceFeatureA, sourceASorted);
                let canonicalFeatureB = select(
                    strongestSourceFeatureA,
                    strongestSourceFeatureB, sourceASorted);
                let canonicalNormal = select(
                    -manifold.normal.xyz, manifold.normal.xyz,
                    sourceASorted);
                append_event(&count, &overflow, EVENT_CONTACT_HIT,
                    canonicalBodyA, canonicalBodyB,
                    body_generation(canonicalBodyA),
                    body_generation(canonicalBodyB),
                    canonicalFeatureA, manifold.pair.ordinal,
                    manifold.state.x, canonicalFeatureB,
                    vec4<f32>(
                        canonicalAnchorA,
                        weightedSeparation * inverseImpulse),
                    vec4<f32>(
                        canonicalAnchorB, normalImpulse),
                    vec4<f32>(canonicalNormal, impactSpeed));
            }
        }
        overflow |= select(0u, 1u, sourceTelemetry[6] != 0u);
    }
    if ((params.counts.w & 2u) != 0u) {
        let islandCount = min(sourceTelemetry[3], params.counts.y);
        // Two stable passes impose type priority while preserving root order.
        for (var index = 0u; index < islandCount; index += 1u) {
            let event = islandEvents[index];
            if (event.eventType == ISLAND_SLEEP) {
                append_event(&count, &overflow, EVENT_ISLAND_SLEEP,
                    event.rootBody, SENTINEL,
                    body_generation(event.rootBody), 0u,
                    0u, event.rootBody,
                    event.bodyCount, 0u,
                    vec4<f32>(0.0), vec4<f32>(0.0),
                    vec4<f32>(0.0));
            }
        }
        for (var index = 0u; index < islandCount; index += 1u) {
            let event = islandEvents[index];
            if (event.eventType == ISLAND_WAKE) {
                append_event(&count, &overflow, EVENT_ISLAND_WAKE,
                    event.rootBody, SENTINEL,
                    body_generation(event.rootBody), 0u,
                    0u, event.rootBody,
                    event.bodyCount, 0u,
                    vec4<f32>(0.0), vec4<f32>(0.0),
                    vec4<f32>(0.0));
            }
        }
        overflow |= select(0u, 1u, sourceTelemetry[4] != 0u);
    }
    if ((params.counts.w & 8u) != 0u) {
        for (var slot = 1u; slot < params.attachments.x; slot += 1u) {
            let attachment = attachments[slot];
            if ((attachment.identity.y & ATTACHMENT_BROKEN) != 0u
                && attachment.breakContext.x == params.tick.x) {
                append_event(&count, &overflow, EVENT_ATTACHMENT_BREAK,
                    attachment.identity.z, attachment.identity.w,
                    attachment.bodyGenerations.x,
                    attachment.bodyGenerations.y,
                    attachment.identity.x, slot,
                    bitcast<u32>(attachment.evidence.x),
                    bitcast<u32>(attachment.evidence.y),
                    vec4<f32>(0.0), vec4<f32>(0.0),
                    vec4<f32>(0.0));
            }
        }
    }
    packet[0] = vec4<u32>(min(count, params.counts.z), overflow,
                           params.tick.x, params.tick.y);
}
