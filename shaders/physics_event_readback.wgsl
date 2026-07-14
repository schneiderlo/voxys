const CONTACT_BEGIN : u32 = 1u;
const CONTACT_END : u32 = 2u;
const EVENT_CONTACT_HIT : u32 = 3u;
const ISLAND_SLEEP : u32 = 1u;
const ISLAND_WAKE : u32 = 2u;
const EVENT_CONTACT_BEGIN : u32 = 1u;
const EVENT_CONTACT_END : u32 = 2u;
const EVENT_ISLAND_SLEEP : u32 = 4u;
const EVENT_ISLAND_WAKE : u32 = 5u;
const SENTINEL : u32 = 0xffffffffu;

struct ContactEvent {
    pairLow : u32,
    pairHigh : u32,
    eventType : u32,
    contactId : u32,
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

struct EventParams {
    counts : vec4<u32>,
    tick : vec4<u32>,
};

@group(0) @binding(0) var<storage, read> contactEvents : array<ContactEvent>;
@group(0) @binding(1) var<storage, read> contactTelemetry : array<u32>;
@group(0) @binding(2) var<storage, read> islandEvents : array<IslandEvent>;
@group(0) @binding(3) var<storage, read> islandTelemetry : array<u32>;
@group(0) @binding(4) var<storage, read_write> packet : array<vec4<u32>>;
@group(0) @binding(5) var<storage, read> manifolds : array<ContactManifold>;
@group(0) @binding(6) var<storage, read> narrowTelemetry : array<u32>;
@group(0) @binding(7) var<uniform> params : EventParams;

fn append_event(count : ptr<function, u32>, overflow : ptr<function, u32>,
                eventType : u32, bodyA : u32, bodyB : u32,
                feature : u32, sourceId : u32, auxiliary : u32,
                flags : u32) {
    let output = *count;
    if (output < params.counts.z) {
        let word = 1u + output * 2u;
        packet[word] = vec4<u32>(params.tick.x, eventType, bodyA, bodyB);
        packet[word + 1u] = vec4<u32>(
            feature, sourceId, auxiliary, flags);
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
        let beginCount = min(contactTelemetry[7], params.counts.x);
        let endCount = min(contactTelemetry[8], params.counts.x);
        for (var index = 0u; index < beginCount; index += 1u) {
            let event = contactEvents[index];
            if (event.eventType == CONTACT_BEGIN) {
                append_event(&count, &overflow, EVENT_CONTACT_BEGIN,
                    event.pairLow, event.pairHigh, 0u, event.contactId, 0u, 0u);
            }
        }
        for (var index = 0u; index < endCount; index += 1u) {
            let event = contactEvents[params.counts.x + index];
            if (event.eventType == CONTACT_END) {
                append_event(&count, &overflow, EVENT_CONTACT_END,
                    event.pairLow, event.pairHigh, 0u, event.contactId, 0u, 0u);
            }
        }
        overflow |= select(0u, 1u, contactTelemetry[12] != 0u);
    }
    if ((params.counts.w & 4u) != 0u) {
        let manifoldCount = min(narrowTelemetry[11], params.tick.z);
        for (var index = 0u; index < manifoldCount; index += 1u) {
            let manifold = manifolds[index];
            var impactSpeed = 0.0;
            var normalImpulse = 0.0;
            if (manifold.state.x > 0u) {
                impactSpeed = max(impactSpeed, manifold.points[0].impulses.y);
                normalImpulse +=
                    manifold.points[0].localAnchorB_normalImpulse.w;
            }
            if (manifold.state.x > 1u) {
                impactSpeed = max(impactSpeed, manifold.points[1].impulses.y);
                normalImpulse +=
                    manifold.points[1].localAnchorB_normalImpulse.w;
            }
            if (manifold.state.x > 2u) {
                impactSpeed = max(impactSpeed, manifold.points[2].impulses.y);
                normalImpulse +=
                    manifold.points[2].localAnchorB_normalImpulse.w;
            }
            if (manifold.state.x > 3u) {
                impactSpeed = max(impactSpeed, manifold.points[3].impulses.y);
                normalImpulse +=
                    manifold.points[3].localAnchorB_normalImpulse.w;
            }
            if (impactSpeed >= 1.0 && normalImpulse > 0.0) {
                append_event(&count, &overflow, EVENT_CONTACT_HIT,
                    manifold.pair.keyHigh, manifold.pair.keyLow,
                    manifold.points[0].features.x, manifold.pair.ordinal,
                    manifold.state.x, 0u);
            }
        }
        overflow |= select(0u, 1u, narrowTelemetry[17] != 0u);
    }
    if ((params.counts.w & 2u) != 0u) {
        let islandCount = min(islandTelemetry[8], params.counts.y);
        // Two stable passes impose type priority while preserving root order.
        for (var index = 0u; index < islandCount; index += 1u) {
            let event = islandEvents[index];
            if (event.eventType == ISLAND_SLEEP) {
                append_event(&count, &overflow, EVENT_ISLAND_SLEEP,
                    event.rootBody, SENTINEL, 0u, event.rootBody,
                    event.bodyCount, 0u);
            }
        }
        for (var index = 0u; index < islandCount; index += 1u) {
            let event = islandEvents[index];
            if (event.eventType == ISLAND_WAKE) {
                append_event(&count, &overflow, EVENT_ISLAND_WAKE,
                    event.rootBody, SENTINEL, 0u, event.rootBody,
                    event.bodyCount, 0u);
            }
        }
        overflow |= select(0u, 1u, islandTelemetry[17] != 0u);
    }
    packet[0] = vec4<u32>(min(count, params.counts.z), overflow,
                           params.tick.x, params.tick.y);
}
