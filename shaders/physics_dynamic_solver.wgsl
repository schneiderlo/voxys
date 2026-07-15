const BODY_ALIVE : u32 = 1u << 20u;
const BODY_AWAKE : u32 = 1u << 21u;
const SENTINEL : u32 = 0xffffffffu;
const SMALL_ISLAND_CONTACT : u32 = 0xfffffffeu;
const SMALL_ISLAND_BODY : u32 = 0x80000000u;
const BODY_DEGREE_MASK : u32 = 0x7fffffffu;

const STAGE_WARM_START : u32 = 0u;
const STAGE_BIASED : u32 = 1u;
const STAGE_RELAX : u32 = 2u;
const STAGE_RESTITUTION : u32 = 3u;
const SERIAL_LEVEL_CONTACT_THRESHOLD : u32 = 1024u;
const SERIAL_WORLD_BODY_CAPACITY : u32 = 256u;
const SERIAL_LEVEL_CAPACITY : u32 = 2u * SERIAL_WORLD_BODY_CAPACITY;

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

struct ConstraintCache {
    normalMass : vec4<f32>,
    preImpactVelocity : vec4<f32>,
    tangentMass : vec4<f32>,
    angularMass : vec4<f32>,
    softness : vec4<f32>,
    sectorOffset : vec4<f32>,
};

struct EndpointDelta {
    linear : vec4<f32>,
    angular : vec4<f32>,
};

struct SolverParams {
    capacities : vec4<u32>,
    control : vec4<u32>,
    gravity_dt : vec4<f32>,
    damping_slop : vec4<f32>,
    solver : vec4<f32>,
    material : vec4<f32>,
};

struct VelocityPair {
    linearA : vec3<f32>,
    angularA : vec3<f32>,
    linearB : vec3<f32>,
    angularB : vec3<f32>,
};

@group(0) @binding(0) var<storage, read_write> poses : array<BodyPose>;
@group(0) @binding(1) var<storage, read_write> motions : array<BodyMotion>;
@group(0) @binding(2) var<storage, read> shapes : array<BodyShape>;
@group(0) @binding(3) var<storage, read_write> metadata : array<vec4<i32>>;
@group(0) @binding(4) var<storage, read_write> manifolds : array<ContactManifold>;
@group(0) @binding(5) var<storage, read> narrowTelemetry : array<u32>;
@group(0) @binding(6) var<storage, read_write> contactColors : array<u32>;
@group(0) @binding(7) var<storage, read_write> acceptedColorMasks : array<atomic<u32>>;
@group(0) @binding(8) var<storage, read_write> colorClaims : array<atomic<u32>>;
@group(0) @binding(9) var<storage, read_write> candidateColors : array<u32>;
@group(0) @binding(10) var<storage, read_write> colorRecords : array<KeyValue>;
@group(0) @binding(11) var<storage, read_write> sortedColorRecords : array<KeyValue>;
@group(0) @binding(12) var<storage, read_write> colorRanges : array<u32>;
@group(0) @binding(13) var<storage, read_write> solverTelemetry : array<atomic<u32>>;
@group(0) @binding(14) var<uniform> params : SolverParams;
@group(0) @binding(15) var<storage, read_write> constraintCaches : array<ConstraintCache>;
@group(0) @binding(16) var<storage, read_write> adjacencyRecords : array<KeyValue>;
@group(0) @binding(17) var<storage, read_write> sortedAdjacency : array<KeyValue>;
@group(0) @binding(18) var<storage, read_write> bodyRanges : array<vec2<u32>>;
@group(0) @binding(19) var<storage, read_write> endpointDeltas : array<EndpointDelta>;
@group(0) @binding(20) var<storage, read_write> bodyDegrees : array<atomic<u32>>;
@group(0) @binding(21) var<storage, read_write> colorDispatchArgs : array<atomic<u32>>;

// Compact worlds keep the single-dispatch path. Dense contact graphs are
// scheduled into dependency levels: contacts in one level touch disjoint
// bodies, while every body's original rank order is preserved across levels.
var<workgroup> serialBodyNextLevel : array<u32, SERIAL_WORLD_BODY_CAPACITY>;
var<workgroup> serialLevelCounts : array<u32, SERIAL_LEVEL_CAPACITY>;
var<workgroup> serialLevelOffsets : array<u32, SERIAL_LEVEL_CAPACITY>;

fn sentinel_record() -> KeyValue {
    return KeyValue(SENTINEL, SENTINEL, SENTINEL, SENTINEL);
}

fn active_contact_count() -> u32 {
    return min(narrowTelemetry[10], params.capacities.y);
}

fn contact_is_active(rank : u32) -> bool {
    return rank < active_contact_count() && manifolds[rank].state.x != 0u;
}

fn body_is_small_island(body : u32) -> bool {
    return (atomicLoad(&bodyDegrees[body]) & SMALL_ISLAND_BODY) != 0u;
}

fn contact_is_small_island(rank : u32) -> bool {
    if ((params.capacities.w & 1u) == 0u || !contact_is_active(rank)) {
        return false;
    }
    let pair = manifolds[rank].pair;
    return body_is_small_island(pair.keyHigh)
        && body_is_small_island(pair.keyLow);
}

fn store_dispatch(slot : u32, x : u32) {
    let base = slot * 4u;
    atomicStore(&colorDispatchArgs[base], x);
    atomicStore(&colorDispatchArgs[base + 1u], 1u);
    atomicStore(&colorDispatchArgs[base + 2u], 1u);
    atomicStore(&colorDispatchArgs[base + 3u], 0u);
}

fn reset_coloring_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index < params.capacities.x) {
        atomicStore(&acceptedColorMasks[index], 0u);
    }
    if (index < params.capacities.y) {
        contactColors[index] = SENTINEL;
        candidateColors[index] = SENTINEL;
        colorRecords[index] = sentinel_record();
    }
    if (index < 39u || (index >= 42u && index < 46u)) {
        atomicStore(&solverTelemetry[index], 0u);
    }
}

@compute @workgroup_size(64)
fn reset_coloring_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    reset_coloring_impl(gid);
}
@compute @workgroup_size(128)
fn reset_coloring_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    reset_coloring_impl(gid);
}
@compute @workgroup_size(256)
fn reset_coloring_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    reset_coloring_impl(gid);
}

fn reset_body_degrees_impl(gid : vec3<u32>) {
    if (gid.x < params.capacities.x) {
        atomicStore(&bodyDegrees[gid.x], 0u);
    }
    if (gid.x == 0u) {
        store_dispatch(params.capacities.z + 1u, 0u);
        store_dispatch(params.capacities.z + 2u, 0u);
        store_dispatch(params.capacities.z + 3u, 0u);
        store_dispatch(params.capacities.z + 4u, 0u);
        store_dispatch(params.capacities.z + 5u, 0u);
    }
}

@compute @workgroup_size(64)
fn reset_body_degrees_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    reset_body_degrees_impl(gid);
}
@compute @workgroup_size(128)
fn reset_body_degrees_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    reset_body_degrees_impl(gid);
}
@compute @workgroup_size(256)
fn reset_body_degrees_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    reset_body_degrees_impl(gid);
}

fn count_body_degrees_impl(gid : vec3<u32>) {
    let rank = gid.x;
    if (!contact_is_active(rank)) { return; }
    let pair = manifolds[rank].pair;
    atomicAdd(&bodyDegrees[pair.keyHigh], 1u);
    atomicAdd(&bodyDegrees[pair.keyLow], 1u);
}

@compute @workgroup_size(64)
fn count_body_degrees_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    count_body_degrees_impl(gid);
}
@compute @workgroup_size(128)
fn count_body_degrees_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    count_body_degrees_impl(gid);
}
@compute @workgroup_size(256)
fn count_body_degrees_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    count_body_degrees_impl(gid);
}

fn mark_small_islands_impl(gid : vec3<u32>) {
    let rank = gid.x;
    if (!contact_is_active(rank)) { return; }
    let pair = manifolds[rank].pair;
    let degreeA = atomicLoad(&bodyDegrees[pair.keyHigh]);
    let degreeB = atomicLoad(&bodyDegrees[pair.keyLow]);
    if ((params.capacities.w & 1u) != 0u
        && degreeA == 1u && degreeB == 1u) {
        atomicOr(&bodyDegrees[pair.keyHigh], SMALL_ISLAND_BODY);
        atomicOr(&bodyDegrees[pair.keyLow], SMALL_ISLAND_BODY);
        contactColors[rank] = SMALL_ISLAND_CONTACT;
        manifolds[rank].state.z &= 255u;
        atomicAdd(&solverTelemetry[43], 1u);
        atomicAdd(&solverTelemetry[44], 2u);
    } else {
        let workgroupSize = params.capacities.w >> 8u;
        store_dispatch(params.capacities.z + 1u,
            (active_contact_count() + workgroupSize - 1u) / workgroupSize);
        store_dispatch(params.capacities.z + 2u, 1u);
        let claimCount = params.capacities.x * params.capacities.z;
        store_dispatch(params.capacities.z + 5u,
            (claimCount + workgroupSize - 1u) / workgroupSize);
    }
}

@compute @workgroup_size(64)
fn mark_small_islands_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_small_islands_impl(gid);
}
@compute @workgroup_size(128)
fn mark_small_islands_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_small_islands_impl(gid);
}
@compute @workgroup_size(256)
fn mark_small_islands_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_small_islands_impl(gid);
}

fn clear_claims_impl(gid : vec3<u32>) {
    let claimCount = params.capacities.x * params.capacities.z;
    if (gid.x < claimCount) {
        atomicStore(&colorClaims[gid.x], SENTINEL);
    }
}

fn clear_round_claims_impl(gid : vec3<u32>) {
    let rank = gid.x;
    if (rank >= active_contact_count()) { return; }
    let color = candidateColors[rank];
    if (color == SENTINEL || color >= params.capacities.z) { return; }
    let pair = manifolds[rank].pair;
    atomicStore(
        &colorClaims[pair.keyHigh * params.capacities.z + color], SENTINEL);
    atomicStore(
        &colorClaims[pair.keyLow * params.capacities.z + color], SENTINEL);
    candidateColors[rank] = SENTINEL;
}

@compute @workgroup_size(64)
fn clear_claims_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    clear_claims_impl(gid);
}
@compute @workgroup_size(128)
fn clear_claims_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    clear_claims_impl(gid);
}
@compute @workgroup_size(256)
fn clear_claims_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    clear_claims_impl(gid);
}

@compute @workgroup_size(64)
fn clear_round_claims_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    clear_round_claims_impl(gid);
}
@compute @workgroup_size(128)
fn clear_round_claims_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    clear_round_claims_impl(gid);
}
@compute @workgroup_size(256)
fn clear_round_claims_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    clear_round_claims_impl(gid);
}

fn choose_contact_color(rank : u32, bodyA : u32, bodyB : u32) -> u32 {
    let unavailable = atomicLoad(&acceptedColorMasks[bodyA])
                    | atomicLoad(&acceptedColorMasks[bodyB]);
    let previousEncoded = (manifolds[rank].state.z >> 8u) & 255u;
    if (previousEncoded != 0u) {
        let previousColor = previousEncoded - 1u;
        if (previousColor < params.capacities.z
            && (unavailable & (1u << previousColor)) == 0u) {
            return previousColor;
        }
    }
    for (var color = 0u; color < params.capacities.z; color += 1u) {
        if ((unavailable & (1u << color)) == 0u) { return color; }
    }
    return SENTINEL;
}

fn claim_colors_impl(gid : vec3<u32>) {
    let rank = gid.x;
    if (!contact_is_active(rank) || contactColors[rank] != SENTINEL) { return; }
    let pair = manifolds[rank].pair;
    let color = choose_contact_color(rank, pair.keyHigh, pair.keyLow);
    candidateColors[rank] = color;
    if (color == SENTINEL) { return; }
    atomicMin(&colorClaims[pair.keyHigh * params.capacities.z + color], rank);
    atomicMin(&colorClaims[pair.keyLow * params.capacities.z + color], rank);
}

@compute @workgroup_size(64)
fn claim_colors_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    claim_colors_impl(gid);
}
@compute @workgroup_size(128)
fn claim_colors_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    claim_colors_impl(gid);
}
@compute @workgroup_size(256)
fn claim_colors_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    claim_colors_impl(gid);
}

fn commit_colors_impl(gid : vec3<u32>) {
    let rank = gid.x;
    if (!contact_is_active(rank) || contactColors[rank] != SENTINEL) { return; }
    let color = candidateColors[rank];
    if (color == SENTINEL) { return; }
    let pair = manifolds[rank].pair;
    if (atomicLoad(&colorClaims[pair.keyHigh * params.capacities.z + color])
            == rank
        && atomicLoad(&colorClaims[pair.keyLow * params.capacities.z + color])
            == rank) {
        contactColors[rank] = color;
        atomicOr(&acceptedColorMasks[pair.keyHigh], 1u << color);
        atomicOr(&acceptedColorMasks[pair.keyLow], 1u << color);
        let previousEncoded = (manifolds[rank].state.z >> 8u) & 255u;
        if (previousEncoded == color + 1u) {
            atomicAdd(&solverTelemetry[35], 1u);
        }
    }
}

@compute @workgroup_size(64)
fn commit_colors_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    commit_colors_impl(gid);
}
@compute @workgroup_size(128)
fn commit_colors_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    commit_colors_impl(gid);
}
@compute @workgroup_size(256)
fn commit_colors_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    commit_colors_impl(gid);
}

fn validate_claim_impl(gid : vec3<u32>) {
    let rank = gid.x;
    if (!contact_is_active(rank)) { return; }
    let color = contactColors[rank];
    if (color >= params.capacities.z) { return; }
    let pair = manifolds[rank].pair;
    atomicMin(&colorClaims[pair.keyHigh * params.capacities.z + color], rank);
    atomicMin(&colorClaims[pair.keyLow * params.capacities.z + color], rank);
}

@compute @workgroup_size(64)
fn validate_claim_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    validate_claim_impl(gid);
}
@compute @workgroup_size(128)
fn validate_claim_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    validate_claim_impl(gid);
}
@compute @workgroup_size(256)
fn validate_claim_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    validate_claim_impl(gid);
}

fn build_color_records_impl(gid : vec3<u32>) {
    let rank = gid.x;
    if (rank >= params.capacities.y) { return; }
    colorRecords[rank] = sentinel_record();
    if (!contact_is_active(rank)
        || contactColors[rank] == SMALL_ISLAND_CONTACT) { return; }
    let pair = manifolds[rank].pair;
    var color = contactColors[rank];
    if (color == SENTINEL) { color = params.capacities.z; }
    colorRecords[rank] = KeyValue(rank, color, rank, rank);
    manifolds[rank].state.z = (manifolds[rank].state.z & 255u)
        | ((color + 1u) << 8u);
    if (color < params.capacities.z) {
        atomicAdd(&solverTelemetry[color], 1u);
        atomicAdd(&solverTelemetry[33], 1u);
        if (atomicLoad(
            &colorClaims[pair.keyHigh * params.capacities.z + color]) != rank
            || atomicLoad(
            &colorClaims[pair.keyLow * params.capacities.z + color]) != rank) {
            atomicAdd(&solverTelemetry[37], 1u);
        }
    } else {
        atomicAdd(&solverTelemetry[34], 1u);
    }
}

@compute @workgroup_size(64)
fn build_color_records_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    build_color_records_impl(gid);
}
@compute @workgroup_size(128)
fn build_color_records_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    build_color_records_impl(gid);
}
@compute @workgroup_size(256)
fn build_color_records_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    build_color_records_impl(gid);
}

fn lower_color_bound(color : u32, count : u32) -> u32 {
    var first = 0u;
    var limit = count;
    while (first < limit) {
        let middle = first + (limit - first) / 2u;
        if (sortedColorRecords[middle].keyHigh < color) {
            first = middle + 1u;
        } else {
            limit = middle;
        }
    }
    return first;
}

fn upper_color_bound(color : u32, count : u32) -> u32 {
    var first = 0u;
    var limit = count;
    while (first < limit) {
        let middle = first + (limit - first) / 2u;
        if (sortedColorRecords[middle].keyHigh <= color) {
            first = middle + 1u;
        } else {
            limit = middle;
        }
    }
    return first;
}

fn build_color_ranges_impl(localIndex : u32) {
    let globalSortBase = (params.capacities.z + 1u) * 4u;
    let hasSortedRecords =
        atomicLoad(&colorDispatchArgs[globalSortBase]) != 0u;
    if (localIndex <= params.capacities.z) {
        let sortedCount = select(0u, active_contact_count(), hasSortedRecords);
        let first = lower_color_bound(localIndex, sortedCount);
        let last = upper_color_bound(localIndex, sortedCount);
        let count = last - first;
        colorRanges[localIndex * 2u] = first;
        colorRanges[localIndex * 2u + 1u] = count;
        let workgroupSize = params.capacities.w >> 8u;
        store_dispatch(localIndex,
            (count + workgroupSize - 1u) / workgroupSize);
    }
    storageBarrier();
    workgroupBarrier();
    if (localIndex != 0u) { return; }
    let activeContacts = atomicLoad(&solverTelemetry[33])
        + atomicLoad(&solverTelemetry[34])
        + atomicLoad(&solverTelemetry[43]);
    atomicStore(&solverTelemetry[32], activeContacts);
    atomicMax(&solverTelemetry[39], activeContacts);
    let overflow = colorRanges[params.capacities.z * 2u + 1u];
    if (overflow != 0u) {
        let workgroupSize = params.capacities.w >> 8u;
        store_dispatch(params.capacities.z + 3u,
            (overflow * 2u + workgroupSize - 1u)
                / workgroupSize);
        store_dispatch(params.capacities.z + 4u, 1u);
    }
    atomicMax(&solverTelemetry[40], overflow);
    atomicStore(&solverTelemetry[38], params.control.y);
    atomicStore(&solverTelemetry[42], select(0u, 1u,
        narrowTelemetry[10] > params.capacities.y));
}

@compute @workgroup_size(64)
fn build_color_ranges_64(
    @builtin(local_invocation_index) localIndex : u32) {
    build_color_ranges_impl(localIndex);
}
@compute @workgroup_size(128)
fn build_color_ranges_128(
    @builtin(local_invocation_index) localIndex : u32) {
    build_color_ranges_impl(localIndex);
}
@compute @workgroup_size(256)
fn build_color_ranges_256(
    @builtin(local_invocation_index) localIndex : u32) {
    build_color_ranges_impl(localIndex);
}

fn clear_adjacency_impl(gid : vec3<u32>) {
    if (gid.x < params.capacities.x) {
        bodyRanges[gid.x] = vec2<u32>(0u);
    }
}

@compute @workgroup_size(64)
fn clear_adjacency_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    clear_adjacency_impl(gid);
}
@compute @workgroup_size(128)
fn clear_adjacency_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    clear_adjacency_impl(gid);
}
@compute @workgroup_size(256)
fn clear_adjacency_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    clear_adjacency_impl(gid);
}

fn emit_adjacency_impl(gid : vec3<u32>) {
    let localIndex = gid.x;
    let rangeBase = params.capacities.z * 2u;
    let count = colorRanges[rangeBase + 1u];
    if (localIndex >= count) { return; }
    let record = sortedColorRecords[colorRanges[rangeBase] + localIndex];
    let pair = manifolds[record.value].pair;
    let firstEndpoint = localIndex * 2u;
    adjacencyRecords[firstEndpoint] = KeyValue(
        record.value, pair.keyHigh, firstEndpoint, record.value * 2u);
    adjacencyRecords[firstEndpoint + 1u] = KeyValue(
        record.value, pair.keyLow, firstEndpoint + 1u,
        record.value * 2u + 1u);
}

@compute @workgroup_size(64)
fn emit_adjacency_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    emit_adjacency_impl(gid);
}
@compute @workgroup_size(128)
fn emit_adjacency_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    emit_adjacency_impl(gid);
}
@compute @workgroup_size(256)
fn emit_adjacency_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    emit_adjacency_impl(gid);
}

fn upper_body_bound(body : u32, firstIndex : u32, count : u32) -> u32 {
    var first = firstIndex;
    var limit = count;
    while (first < limit) {
        let middle = first + (limit - first) / 2u;
        if (sortedAdjacency[middle].keyHigh <= body) {
            first = middle + 1u;
        } else {
            limit = middle;
        }
    }
    return first;
}

fn build_body_ranges_impl(gid : vec3<u32>) {
    let index = gid.x;
    let endpointCount =
        colorRanges[params.capacities.z * 2u + 1u] * 2u;
    if (index >= endpointCount) { return; }
    let body = sortedAdjacency[index].keyHigh;
    if (body >= params.capacities.x
        || (index != 0u && sortedAdjacency[index - 1u].keyHigh == body)) {
        return;
    }
    let last = upper_body_bound(body, index + 1u, endpointCount);
    let degree = last - index;
    bodyRanges[body] = vec2<u32>(index, degree);
    atomicMax(&solverTelemetry[36], degree);
}

@compute @workgroup_size(64)
fn build_body_ranges_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    build_body_ranges_impl(gid);
}
@compute @workgroup_size(128)
fn build_body_ranges_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    build_body_ranges_impl(gid);
}
@compute @workgroup_size(256)
fn build_body_ranges_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    build_body_ranges_impl(gid);
}

fn quaternion_multiply(a : vec4<f32>, b : vec4<f32>) -> vec4<f32> {
    return vec4<f32>(
        a.w * b.xyz + b.w * a.xyz + cross(a.xyz, b.xyz),
        a.w * b.w - dot(a.xyz, b.xyz));
}

fn quaternion_rotate(q : vec4<f32>, value : vec3<f32>) -> vec3<f32> {
    let twiceCross = 2.0 * cross(q.xyz, value);
    return value + q.w * twiceCross + cross(q.xyz, twiceCross);
}

fn quaternion_inverse_rotate(q : vec4<f32>, value : vec3<f32>) -> vec3<f32> {
    return quaternion_rotate(vec4<f32>(-q.xyz, q.w), value);
}

fn adjacent_sector_delta(reference : i32, other : i32) -> i32 {
    if (other == reference) { return 0; }
    if (reference < 2147483647 && other == reference + 1) { return 1; }
    if (reference > -2147483647 - 1 && other == reference - 1) { return -1; }
    return 0x3fffffff;
}

fn body_sector_offset_in_frame(body : u32,
                               frameBody : u32) -> vec3<f32> {
    var sectorDelta = vec3<i32>(0);
    for (var axis = 0u; axis < 3u; axis += 1u) {
        sectorDelta[axis] = adjacent_sector_delta(
            metadata[frameBody][axis], metadata[body][axis]);
        if (abs(sectorDelta[axis]) > 1) {
            return vec3<f32>(3.402823466e+38);
        }
    }
    return vec3<f32>(sectorDelta) * 256.0;
}

fn body_position_in_frame(body : u32, frameBody : u32) -> vec3<f32> {
    return poses[body].position_invMass.xyz
         + body_sector_offset_in_frame(body, frameBody);
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

fn inverse_inertia_state(pose : BodyPose, shape : BodyShape,
                         vector : vec3<f32>) -> vec3<f32> {
    let local = quaternion_inverse_rotate(pose.orientation, vector);
    return quaternion_rotate(
        pose.orientation, local * shape.invInertia_material.xyz);
}

fn inverse_inertia_world(body : u32, vector : vec3<f32>) -> vec3<f32> {
    return inverse_inertia_state(poses[body], shapes[body], vector);
}

fn point_velocity(linear : vec3<f32>, angular : vec3<f32>,
                  lever : vec3<f32>) -> vec3<f32> {
    return linear + cross(angular, lever);
}

fn directional_mass(bodyA : u32, bodyB : u32,
                    leverA : vec3<f32>, leverB : vec3<f32>,
                    direction : vec3<f32>) -> f32 {
    let angularA = inverse_inertia_world(
        bodyA, cross(leverA, direction));
    let angularB = inverse_inertia_world(
        bodyB, cross(leverB, direction));
    let inverseMass = poses[bodyA].position_invMass.w
        + poses[bodyB].position_invMass.w
        + dot(cross(angularA, leverA) + cross(angularB, leverB), direction);
    return select(0.0, 1.0 / inverseMass, inverseMass > 1e-9);
}

fn prepare_constraints_impl(gid : vec3<u32>) {
    let rank = gid.x;
    if (!contact_is_active(rank)) { return; }
    var manifold = manifolds[rank];
    let bodyA = manifold.pair.keyHigh;
    let bodyB = manifold.pair.keyLow;
    let normal = manifold.normal.xyz;
    let tangent1 = manifold.tangent1.xyz;
    let tangent2 = manifold.tangent2.xyz;
    let linearA = motions[bodyA].linearVelocity_sleep.xyz;
    let angularA = motions[bodyA].angularVelocity_flags.xyz;
    let linearB = motions[bodyB].linearVelocity_sleep.xyz;
    let angularB = motions[bodyB].angularVelocity_flags.xyz;
    var cache : ConstraintCache;
    cache.normalMass = vec4<f32>(0.0);
    cache.preImpactVelocity = vec4<f32>(0.0);
    cache.tangentMass = vec4<f32>(0.0);
    cache.angularMass = vec4<f32>(0.0);
    cache.softness = vec4<f32>(params.solver.y, 1.0, 0.0, 0.0);
    cache.sectorOffset = vec4<f32>(
        body_sector_offset_in_frame(bodyB, bodyA), 0.0);
    for (var pointIndex = 0u; pointIndex < manifold.state.x;
         pointIndex += 1u) {
        let leverA = quaternion_rotate(
            poses[bodyA].orientation,
            manifold.points[pointIndex].localAnchorA_separation.xyz);
        let leverB = quaternion_rotate(
            poses[bodyB].orientation,
            manifold.points[pointIndex].localAnchorB_normalImpulse.xyz);
        cache.normalMass[pointIndex] = directional_mass(
            bodyA, bodyB, leverA, leverB, normal);
        cache.preImpactVelocity[pointIndex] = dot(
            point_velocity(linearB, angularB, leverB)
                - point_velocity(linearA, angularA, leverA), normal);
        // This lane is per-tick event state, unlike the persistent warm-start
        // impulse in localAnchorB_normalImpulse.w.
        manifold.points[pointIndex].impulses.y =
            max(-cache.preImpactVelocity[pointIndex], 0.0);
    }
    let leverA = quaternion_rotate(
        poses[bodyA].orientation, manifold.frictionAnchorA.xyz);
    let leverB = quaternion_rotate(
        poses[bodyB].orientation, manifold.frictionAnchorB.xyz);
    let inverseMass = poses[bodyA].position_invMass.w
                    + poses[bodyB].position_invMass.w;
    let angularA1 = inverse_inertia_world(bodyA, cross(leverA, tangent1));
    let angularB1 = inverse_inertia_world(bodyB, cross(leverB, tangent1));
    let angularA2 = inverse_inertia_world(bodyA, cross(leverA, tangent2));
    let angularB2 = inverse_inertia_world(bodyB, cross(leverB, tangent2));
    let k00 = inverseMass + dot(
        cross(angularA1, leverA) + cross(angularB1, leverB), tangent1);
    let k01 = dot(
        cross(angularA1, leverA) + cross(angularB1, leverB), tangent2);
    let k11 = inverseMass + dot(
        cross(angularA2, leverA) + cross(angularB2, leverB), tangent2);
    let determinant = k00 * k11 - k01 * k01;
    if (determinant > 1e-9) {
        cache.tangentMass = vec4<f32>(
            k11 / determinant, -k01 / determinant,
            k00 / determinant, 0.0);
    }
    let twistInverse = dot(normal,
        inverse_inertia_world(bodyA, normal)
        + inverse_inertia_world(bodyB, normal));
    let rollingInverse = dot(tangent1,
        inverse_inertia_world(bodyA, tangent1)
        + inverse_inertia_world(bodyB, tangent1));
    var frictionRadius = 0.0;
    for (var pointIndex = 0u; pointIndex < manifold.state.x;
         pointIndex += 1u) {
        frictionRadius = max(frictionRadius, length(
            manifold.points[pointIndex].localAnchorA_separation.xyz
                - manifold.frictionAnchorA.xyz));
    }
    cache.angularMass = vec4<f32>(
        select(0.0, 1.0 / twistInverse, twistInverse > 1e-9),
        select(0.0, 1.0 / rollingInverse, rollingInverse > 1e-9),
        max(frictionRadius, params.damping_slop.z), 0.0);
    manifolds[rank] = manifold;
    constraintCaches[rank] = cache;
}

@compute @workgroup_size(64)
fn prepare_constraints_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    prepare_constraints_impl(gid);
}
@compute @workgroup_size(128)
fn prepare_constraints_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    prepare_constraints_impl(gid);
}
@compute @workgroup_size(256)
fn prepare_constraints_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    prepare_constraints_impl(gid);
}

fn apply_impulse(velocities : ptr<function, VelocityPair>,
                 bodyA : u32, bodyB : u32,
                 leverA : vec3<f32>, leverB : vec3<f32>,
                 impulse : vec3<f32>) {
    (*velocities).linearA -= impulse * poses[bodyA].position_invMass.w;
    (*velocities).angularA -= inverse_inertia_world(
        bodyA, cross(leverA, impulse));
    (*velocities).linearB += impulse * poses[bodyB].position_invMass.w;
    (*velocities).angularB += inverse_inertia_world(
        bodyB, cross(leverB, impulse));
}

fn apply_angular_impulse(velocities : ptr<function, VelocityPair>,
                         bodyA : u32, bodyB : u32,
                         impulse : vec3<f32>) {
    (*velocities).angularA -= inverse_inertia_world(bodyA, impulse);
    (*velocities).angularB += inverse_inertia_world(bodyB, impulse);
}

fn solve_contact(rank : u32, stage : u32) -> VelocityPair {
    var manifold = manifolds[rank];
    let cache = constraintCaches[rank];
    let bodyA = manifold.pair.keyHigh;
    let bodyB = manifold.pair.keyLow;
    let normal = manifold.normal.xyz;
    let tangent1 = manifold.tangent1.xyz;
    let tangent2 = manifold.tangent2.xyz;
    var velocities : VelocityPair;
    velocities.linearA = motions[bodyA].linearVelocity_sleep.xyz;
    velocities.angularA = motions[bodyA].angularVelocity_flags.xyz;
    velocities.linearB = motions[bodyB].linearVelocity_sleep.xyz;
    velocities.angularB = motions[bodyB].angularVelocity_flags.xyz;
    for (var pointIndex = 0u; pointIndex < manifold.state.x;
         pointIndex += 1u) {
        let leverA = quaternion_rotate(
            poses[bodyA].orientation,
            manifold.points[pointIndex].localAnchorA_separation.xyz);
        let leverB = quaternion_rotate(
            poses[bodyB].orientation,
            manifold.points[pointIndex].localAnchorB_normalImpulse.xyz);
        if (stage == STAGE_WARM_START) {
            apply_impulse(&velocities, bodyA, bodyB, leverA, leverB,
                normal * manifold.points[pointIndex]
                    .localAnchorB_normalImpulse.w);
            continue;
        }
        let relativeVelocity = point_velocity(
            velocities.linearB, velocities.angularB, leverB)
            - point_velocity(velocities.linearA, velocities.angularA, leverA);
        let normalVelocity = dot(relativeVelocity, normal);
        var incremental = 0.0;
        if (stage == STAGE_RESTITUTION) {
            if (cache.preImpactVelocity[pointIndex]
                    < -params.material.w
                && manifold.points[pointIndex]
                    .localAnchorB_normalImpulse.w > 0.0) {
                let restitutionVelocity = -params.material.y
                    * cache.preImpactVelocity[pointIndex];
                incremental = cache.normalMass[pointIndex]
                    * max(restitutionVelocity - normalVelocity, 0.0);
            }
        } else {
            var bias = 0.0;
            var massScale = 1.0;
            var impulseScale = 0.0;
            if (stage == STAGE_BIASED) {
                let relativeCenter = cache.sectorOffset.xyz
                    + poses[bodyB].position_invMass.xyz
                    - poses[bodyA].position_invMass.xyz;
                let separation = dot(
                    relativeCenter + leverB - leverA, normal);
                if (separation > 0.0) {
                    bias = separation / max(params.gravity_dt.w, 1e-7);
                } else {
                    bias = max(cache.softness.x * separation
                        / max(params.gravity_dt.w, 1e-7), -params.solver.z);
                }
                massScale = cache.softness.y;
                impulseScale = cache.softness.z;
            }
            let oldImpulse = manifold.points[pointIndex]
                .localAnchorB_normalImpulse.w;
            incremental = -massScale * cache.normalMass[pointIndex]
                * (normalVelocity + bias) - impulseScale * oldImpulse;
            let newImpulse = max(oldImpulse + incremental, 0.0);
            incremental = newImpulse - oldImpulse;
            manifold.points[pointIndex].localAnchorB_normalImpulse.w =
                newImpulse;
            manifold.points[pointIndex].impulses.x = newImpulse;
        }
        apply_impulse(&velocities, bodyA, bodyB, leverA, leverB,
                      normal * incremental);
    }

    if (stage == STAGE_WARM_START || stage == STAGE_RELAX) {
        let leverA = quaternion_rotate(
            poses[bodyA].orientation, manifold.frictionAnchorA.xyz);
        let leverB = quaternion_rotate(
            poses[bodyB].orientation, manifold.frictionAnchorB.xyz);
        if (stage == STAGE_WARM_START) {
            let tangentImpulse = tangent1 * manifold.tangent1.w
                               + tangent2 * manifold.tangent2.w;
            apply_impulse(&velocities, bodyA, bodyB, leverA, leverB,
                          tangentImpulse);
            apply_angular_impulse(&velocities, bodyA, bodyB,
                normal * manifold.frictionAnchorA.w
                + manifold.rollingImpulse.xyz);
        } else {
            var normalSum = 0.0;
            for (var pointIndex = 0u; pointIndex < manifold.state.x;
                 pointIndex += 1u) {
                normalSum += manifold.points[pointIndex]
                    .localAnchorB_normalImpulse.w;
            }
            let relativeVelocity = point_velocity(
                velocities.linearB, velocities.angularB, leverB)
                - point_velocity(velocities.linearA, velocities.angularA,
                                 leverA);
            let velocity1 = dot(relativeVelocity, tangent1);
            let velocity2 = dot(relativeVelocity, tangent2);
            let oldFriction = vec2<f32>(
                manifold.tangent1.w, manifold.tangent2.w);
            var newFriction = oldFriction - vec2<f32>(
                cache.tangentMass.x * velocity1
                    + cache.tangentMass.y * velocity2,
                cache.tangentMass.y * velocity1
                    + cache.tangentMass.z * velocity2);
            let frictionLimit = params.material.x * normalSum;
            let frictionLength = length(newFriction);
            if (frictionLength > frictionLimit && frictionLength > 0.0) {
                newFriction *= frictionLimit / frictionLength;
            }
            let frictionDelta = newFriction - oldFriction;
            manifold.tangent1.w = newFriction.x;
            manifold.tangent2.w = newFriction.y;
            apply_impulse(&velocities, bodyA, bodyB, leverA, leverB,
                tangent1 * frictionDelta.x + tangent2 * frictionDelta.y);

            let relativeAngular = velocities.angularB - velocities.angularA;
            let oldTwist = manifold.frictionAnchorA.w;
            let twistLimit = params.material.x * normalSum
                           * cache.angularMass.z;
            let newTwist = clamp(oldTwist
                - cache.angularMass.x * dot(relativeAngular, normal),
                -twistLimit, twistLimit);
            manifold.frictionAnchorA.w = newTwist;
            apply_angular_impulse(&velocities, bodyA, bodyB,
                                  normal * (newTwist - oldTwist));

            let rollingVelocity = relativeAngular
                - normal * dot(relativeAngular, normal);
            let oldRolling = manifold.rollingImpulse.xyz;
            var newRolling = oldRolling
                - cache.angularMass.y * rollingVelocity;
            let rollingLimit = params.material.z * normalSum;
            let rollingLength = length(newRolling);
            if (rollingLength > rollingLimit && rollingLength > 0.0) {
                newRolling *= rollingLimit / rollingLength;
            }
            manifold.rollingImpulse = vec4<f32>(newRolling, 0.0);
            apply_angular_impulse(&velocities, bodyA, bodyB,
                                  newRolling - oldRolling);
        }
    }
    manifolds[rank] = manifold;
    return velocities;
}

fn apply_impulse_state(velocities : ptr<function, VelocityPair>,
                       poseA : BodyPose, shapeA : BodyShape,
                       poseB : BodyPose, shapeB : BodyShape,
                       leverA : vec3<f32>, leverB : vec3<f32>,
                       impulse : vec3<f32>) {
    (*velocities).linearA -= impulse * poseA.position_invMass.w;
    (*velocities).angularA -= inverse_inertia_state(
        poseA, shapeA, cross(leverA, impulse));
    (*velocities).linearB += impulse * poseB.position_invMass.w;
    (*velocities).angularB += inverse_inertia_state(
        poseB, shapeB, cross(leverB, impulse));
}

fn apply_angular_impulse_state(velocities : ptr<function, VelocityPair>,
                               poseA : BodyPose, shapeA : BodyShape,
                               poseB : BodyPose, shapeB : BodyShape,
                               impulse : vec3<f32>) {
    (*velocities).angularA -= inverse_inertia_state(poseA, shapeA, impulse);
    (*velocities).angularB += inverse_inertia_state(poseB, shapeB, impulse);
}

fn solve_contact_state(manifoldState : ptr<function, ContactManifold>,
                       cache : ConstraintCache,
                       poseA : BodyPose, motionA : BodyMotion,
                       shapeA : BodyShape,
                       poseB : BodyPose, motionB : BodyMotion,
                       shapeB : BodyShape, stage : u32) -> VelocityPair {
    var manifold = *manifoldState;
    let normal = manifold.normal.xyz;
    let tangent1 = manifold.tangent1.xyz;
    let tangent2 = manifold.tangent2.xyz;
    var velocities : VelocityPair;
    velocities.linearA = motionA.linearVelocity_sleep.xyz;
    velocities.angularA = motionA.angularVelocity_flags.xyz;
    velocities.linearB = motionB.linearVelocity_sleep.xyz;
    velocities.angularB = motionB.angularVelocity_flags.xyz;

    for (var pointIndex = 0u; pointIndex < manifold.state.x;
         pointIndex += 1u) {
        let worldA = poseA.position_invMass.xyz + quaternion_rotate(
            poseA.orientation,
            manifold.points[pointIndex].localAnchorA_separation.xyz);
        let worldB = poseB.position_invMass.xyz + quaternion_rotate(
            poseB.orientation,
            manifold.points[pointIndex].localAnchorB_normalImpulse.xyz);
        let leverA = worldA - poseA.position_invMass.xyz;
        let leverB = worldB - poseB.position_invMass.xyz;
        if (stage == STAGE_WARM_START) {
            apply_impulse_state(&velocities, poseA, shapeA, poseB, shapeB,
                leverA, leverB, normal * manifold.points[pointIndex]
                    .localAnchorB_normalImpulse.w);
            continue;
        }
        let relativeVelocity = point_velocity(
            velocities.linearB, velocities.angularB, leverB)
            - point_velocity(velocities.linearA, velocities.angularA, leverA);
        let normalVelocity = dot(relativeVelocity, normal);
        var incremental = 0.0;
        if (stage == STAGE_RESTITUTION) {
            if (cache.preImpactVelocity[pointIndex] < -params.material.w
                && manifold.points[pointIndex]
                    .localAnchorB_normalImpulse.w > 0.0) {
                let restitutionVelocity = -params.material.y
                    * cache.preImpactVelocity[pointIndex];
                incremental = cache.normalMass[pointIndex]
                    * max(restitutionVelocity - normalVelocity, 0.0);
            }
        } else {
            var bias = 0.0;
            var massScale = 1.0;
            var impulseScale = 0.0;
            if (stage == STAGE_BIASED) {
                let separation = dot(worldB - worldA, normal);
                if (separation > 0.0) {
                    bias = separation / max(params.gravity_dt.w, 1e-7);
                } else {
                    bias = max(cache.softness.x * separation
                        / max(params.gravity_dt.w, 1e-7), -params.solver.z);
                }
                massScale = cache.softness.y;
                impulseScale = cache.softness.z;
            }
            let oldImpulse = manifold.points[pointIndex]
                .localAnchorB_normalImpulse.w;
            incremental = -massScale * cache.normalMass[pointIndex]
                * (normalVelocity + bias) - impulseScale * oldImpulse;
            let newImpulse = max(oldImpulse + incremental, 0.0);
            incremental = newImpulse - oldImpulse;
            manifold.points[pointIndex].localAnchorB_normalImpulse.w =
                newImpulse;
            manifold.points[pointIndex].impulses.x = newImpulse;
        }
        apply_impulse_state(&velocities, poseA, shapeA, poseB, shapeB,
            leverA, leverB, normal * incremental);
    }

    if (stage == STAGE_WARM_START || stage == STAGE_RELAX) {
        let centerA = poseA.position_invMass.xyz + quaternion_rotate(
            poseA.orientation, manifold.frictionAnchorA.xyz);
        let centerB = poseB.position_invMass.xyz + quaternion_rotate(
            poseB.orientation, manifold.frictionAnchorB.xyz);
        let leverA = centerA - poseA.position_invMass.xyz;
        let leverB = centerB - poseB.position_invMass.xyz;
        if (stage == STAGE_WARM_START) {
            let tangentImpulse = tangent1 * manifold.tangent1.w
                               + tangent2 * manifold.tangent2.w;
            apply_impulse_state(&velocities, poseA, shapeA, poseB, shapeB,
                leverA, leverB, tangentImpulse);
            apply_angular_impulse_state(
                &velocities, poseA, shapeA, poseB, shapeB,
                normal * manifold.frictionAnchorA.w
                    + manifold.rollingImpulse.xyz);
        } else {
            var normalSum = 0.0;
            for (var pointIndex = 0u; pointIndex < manifold.state.x;
                 pointIndex += 1u) {
                normalSum += manifold.points[pointIndex]
                    .localAnchorB_normalImpulse.w;
            }
            let relativeVelocity = point_velocity(
                velocities.linearB, velocities.angularB, leverB)
                - point_velocity(velocities.linearA, velocities.angularA,
                                 leverA);
            let velocity1 = dot(relativeVelocity, tangent1);
            let velocity2 = dot(relativeVelocity, tangent2);
            let oldFriction = vec2<f32>(
                manifold.tangent1.w, manifold.tangent2.w);
            var newFriction = oldFriction - vec2<f32>(
                cache.tangentMass.x * velocity1
                    + cache.tangentMass.y * velocity2,
                cache.tangentMass.y * velocity1
                    + cache.tangentMass.z * velocity2);
            let frictionLimit = params.material.x * normalSum;
            let frictionLength = length(newFriction);
            if (frictionLength > frictionLimit && frictionLength > 0.0) {
                newFriction *= frictionLimit / frictionLength;
            }
            let frictionDelta = newFriction - oldFriction;
            manifold.tangent1.w = newFriction.x;
            manifold.tangent2.w = newFriction.y;
            apply_impulse_state(&velocities, poseA, shapeA, poseB, shapeB,
                leverA, leverB,
                tangent1 * frictionDelta.x + tangent2 * frictionDelta.y);

            let relativeAngular = velocities.angularB - velocities.angularA;
            let oldTwist = manifold.frictionAnchorA.w;
            let twistLimit = params.material.x * normalSum
                           * cache.angularMass.z;
            let newTwist = clamp(oldTwist
                - cache.angularMass.x * dot(relativeAngular, normal),
                -twistLimit, twistLimit);
            manifold.frictionAnchorA.w = newTwist;
            apply_angular_impulse_state(
                &velocities, poseA, shapeA, poseB, shapeB,
                normal * (newTwist - oldTwist));

            let rollingVelocity = relativeAngular
                - normal * dot(relativeAngular, normal);
            let oldRolling = manifold.rollingImpulse.xyz;
            var newRolling = oldRolling
                - cache.angularMass.y * rollingVelocity;
            let rollingLimit = params.material.z * normalSum;
            let rollingLength = length(newRolling);
            if (rollingLength > rollingLimit && rollingLength > 0.0) {
                newRolling *= rollingLimit / rollingLength;
            }
            manifold.rollingImpulse = vec4<f32>(newRolling, 0.0);
            apply_angular_impulse_state(
                &velocities, poseA, shapeA, poseB, shapeB,
                newRolling - oldRolling);
        }
    }
    *manifoldState = manifold;
    return velocities;
}

fn store_contact_velocities(pair : KeyValue, velocities : VelocityPair) {
    motions[pair.keyHigh].linearVelocity_sleep = vec4<f32>(
        velocities.linearA, motions[pair.keyHigh].linearVelocity_sleep.w);
    motions[pair.keyHigh].angularVelocity_flags = vec4<f32>(
        velocities.angularA, motions[pair.keyHigh].angularVelocity_flags.w);
    motions[pair.keyLow].linearVelocity_sleep = vec4<f32>(
        velocities.linearB, motions[pair.keyLow].linearVelocity_sleep.w);
    motions[pair.keyLow].angularVelocity_flags = vec4<f32>(
        velocities.angularB, motions[pair.keyLow].angularVelocity_flags.w);
}

fn solve_colored_contact(color : u32, localIndex : u32) {
    let first = colorRanges[color * 2u];
    let count = colorRanges[color * 2u + 1u];
    if (localIndex >= count) { return; }
    let rank = sortedColorRecords[first + localIndex].value;
    let pair = manifolds[rank].pair;
    let velocities = solve_contact(rank, params.control.y);
    store_contact_velocities(pair, velocities);
}

fn solve_colored_impl(gid : vec3<u32>) {
    let color = params.control.x;
    if (color >= params.capacities.z) { return; }
    solve_colored_contact(color, gid.x);
}

@compute @workgroup_size(64)
fn solve_colored_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    solve_colored_impl(gid);
}
@compute @workgroup_size(128)
fn solve_colored_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    solve_colored_impl(gid);
}
@compute @workgroup_size(256)
fn solve_colored_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    solve_colored_impl(gid);
}

fn solve_compact_colors_impl(localIndex : u32) {
    let workgroupSize = params.capacities.w >> 8u;
    for (var color = 0u; color < params.capacities.z; color += 1u) {
        let count = colorRanges[color * 2u + 1u];
        var contact = localIndex;
        while (contact < count) {
            solve_colored_contact(color, contact);
            contact += workgroupSize;
        }
        // Contacts within one color never share a body. The barrier only
        // separates consecutive colors, matching the original dispatch order.
        storageBarrier();
        workgroupBarrier();
    }
}

@compute @workgroup_size(64)
fn solve_compact_colors_64(
    @builtin(local_invocation_index) localIndex : u32) {
    solve_compact_colors_impl(localIndex);
}
@compute @workgroup_size(128)
fn solve_compact_colors_128(
    @builtin(local_invocation_index) localIndex : u32) {
    solve_compact_colors_impl(localIndex);
}
@compute @workgroup_size(256)
fn solve_compact_colors_256(
    @builtin(local_invocation_index) localIndex : u32) {
    solve_compact_colors_impl(localIndex);
}

fn solve_overflow_impl(gid : vec3<u32>) {
    let localIndex = gid.x;
    let rangeBase = params.capacities.z * 2u;
    let first = colorRanges[rangeBase];
    let count = colorRanges[rangeBase + 1u];
    if (localIndex >= count) { return; }
    let rank = sortedColorRecords[first + localIndex].value;
    let pair = manifolds[rank].pair;
    let oldLinearA = motions[pair.keyHigh].linearVelocity_sleep.xyz;
    let oldAngularA = motions[pair.keyHigh].angularVelocity_flags.xyz;
    let oldLinearB = motions[pair.keyLow].linearVelocity_sleep.xyz;
    let oldAngularB = motions[pair.keyLow].angularVelocity_flags.xyz;
    let velocities = solve_contact(rank, params.control.y);
    endpointDeltas[localIndex * 2u].linear = vec4<f32>(
        velocities.linearA - oldLinearA, 0.0);
    endpointDeltas[localIndex * 2u].angular = vec4<f32>(
        velocities.angularA - oldAngularA, 0.0);
    endpointDeltas[localIndex * 2u + 1u].linear = vec4<f32>(
        velocities.linearB - oldLinearB, 0.0);
    endpointDeltas[localIndex * 2u + 1u].angular = vec4<f32>(
        velocities.angularB - oldAngularB, 0.0);
}

@compute @workgroup_size(64)
fn solve_overflow_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    solve_overflow_impl(gid);
}
@compute @workgroup_size(128)
fn solve_overflow_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    solve_overflow_impl(gid);
}
@compute @workgroup_size(256)
fn solve_overflow_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    solve_overflow_impl(gid);
}

fn gather_overflow_impl(gid : vec3<u32>) {
    let body = gid.x;
    if (body >= params.capacities.x) { return; }
    let range = bodyRanges[body];
    var linearDelta = vec3<f32>(0.0);
    var angularDelta = vec3<f32>(0.0);
    for (var localIndex = 0u; localIndex < range.y; localIndex += 1u) {
        let endpoint = sortedAdjacency[range.x + localIndex].value;
        linearDelta += endpointDeltas[endpoint].linear.xyz;
        angularDelta += endpointDeltas[endpoint].angular.xyz;
    }
    if (range.y != 0u) {
        // Overflow constraints are solved Jacobi-style from the same incoming
        // body velocity. Averaging the endpoint corrections prevents a
        // high-degree contact graph from multiplying one body's correction by
        // its degree. Repeated overflow iterations recover convergence.
        let relaxation = 1.0 / f32(range.y);
        motions[body].linearVelocity_sleep = vec4<f32>(
            motions[body].linearVelocity_sleep.xyz
                + linearDelta * relaxation,
            motions[body].linearVelocity_sleep.w);
        motions[body].angularVelocity_flags = vec4<f32>(
            motions[body].angularVelocity_flags.xyz
                + angularDelta * relaxation,
            motions[body].angularVelocity_flags.w);
    }
}

@compute @workgroup_size(64)
fn gather_overflow_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    gather_overflow_impl(gid);
}
@compute @workgroup_size(128)
fn gather_overflow_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    gather_overflow_impl(gid);
}
@compute @workgroup_size(256)
fn gather_overflow_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    gather_overflow_impl(gid);
}

fn integrate_body_velocity(body : u32) {
    if ((u32(metadata[body].w) & (BODY_ALIVE | BODY_AWAKE))
        != (BODY_ALIVE | BODY_AWAKE)) { return; }
    if (poses[body].position_invMass.w <= 1e-7) { return; }
    var linear = motions[body].linearVelocity_sleep.xyz
               + params.gravity_dt.xyz * params.gravity_dt.w;
    var angular = motions[body].angularVelocity_flags.xyz;
    let localAngular = quaternion_inverse_rotate(
        poses[body].orientation, angular);
    let inverseInertia = shapes[body].invInertia_material.xyz;
    let inertia = vec3<f32>(
        select(0.0, 1.0 / inverseInertia.x, inverseInertia.x > 1e-9),
        select(0.0, 1.0 / inverseInertia.y, inverseInertia.y > 1e-9),
        select(0.0, 1.0 / inverseInertia.z, inverseInertia.z > 1e-9));
    let gyroscopicResidual = params.gravity_dt.w
        * cross(localAngular, inertia * localAngular);
    let correctedLocal = localAngular - inverseInertia * gyroscopicResidual;
    angular = quaternion_rotate(poses[body].orientation, correctedLocal);
    linear /= 1.0 + params.damping_slop.x * params.gravity_dt.w;
    angular /= 1.0 + params.damping_slop.y * params.gravity_dt.w;
    motions[body].linearVelocity_sleep = vec4<f32>(
        linear, motions[body].linearVelocity_sleep.w);
    motions[body].angularVelocity_flags = vec4<f32>(
        angular, motions[body].angularVelocity_flags.w);
}

fn integrate_velocities_impl(gid : vec3<u32>) {
    let body = gid.x;
    if (body >= params.capacities.x || body_is_small_island(body)) { return; }
    integrate_body_velocity(body);
}

@compute @workgroup_size(64)
fn integrate_velocities_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    integrate_velocities_impl(gid);
}
@compute @workgroup_size(128)
fn integrate_velocities_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    integrate_velocities_impl(gid);
}
@compute @workgroup_size(256)
fn integrate_velocities_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    integrate_velocities_impl(gid);
}

fn integrate_body_position(body : u32) {
    if ((u32(metadata[body].w) & (BODY_ALIVE | BODY_AWAKE))
        != (BODY_ALIVE | BODY_AWAKE)) { return; }
    if (poses[body].position_invMass.w <= 1e-7) { return; }
    var pose = poses[body];
    pose.position_invMass = vec4<f32>(
        pose.position_invMass.xyz
            + motions[body].linearVelocity_sleep.xyz * params.gravity_dt.w,
        pose.position_invMass.w);
    let omega = vec4<f32>(motions[body].angularVelocity_flags.xyz, 0.0);
    var orientation = pose.orientation + 0.5 * params.gravity_dt.w
        * quaternion_multiply(omega, pose.orientation);
    let squared = dot(orientation, orientation);
    if (squared > 1e-12) { orientation *= inverseSqrt(squared); }
    else { orientation = vec4<f32>(0.0, 0.0, 0.0, 1.0); }
    pose.orientation = orientation;
    var worldMeta = metadata[body];
    // Keep the sector offset cached by constraint preparation valid until the
    // last biased solve. Local coordinates may exceed the canonical interval
    // by at most one tick of bounded motion.
    if (params.control.z + 1u >= params.control.w) {
        normalize_world_position(&pose, &worldMeta);
    }
    poses[body] = pose;
    metadata[body] = worldMeta;
}

fn integrate_body_position_serial(body : u32, normalize : bool) {
    if ((u32(metadata[body].w) & (BODY_ALIVE | BODY_AWAKE))
        != (BODY_ALIVE | BODY_AWAKE)) { return; }
    if (poses[body].position_invMass.w <= 1e-7) { return; }
    var pose = poses[body];
    pose.position_invMass = vec4<f32>(
        pose.position_invMass.xyz
            + motions[body].linearVelocity_sleep.xyz * params.gravity_dt.w,
        pose.position_invMass.w);
    let omega = vec4<f32>(motions[body].angularVelocity_flags.xyz, 0.0);
    var orientation = pose.orientation + 0.5 * params.gravity_dt.w
        * quaternion_multiply(omega, pose.orientation);
    let squared = dot(orientation, orientation);
    if (squared > 1e-12) { orientation *= inverseSqrt(squared); }
    else { orientation = vec4<f32>(0.0, 0.0, 0.0, 1.0); }
    pose.orientation = orientation;
    var worldMeta = metadata[body];
    if (normalize) { normalize_world_position(&pose, &worldMeta); }
    poses[body] = pose;
    metadata[body] = worldMeta;
}

fn integrate_positions_impl(gid : vec3<u32>) {
    let body = gid.x;
    if (body >= params.capacities.x || body_is_small_island(body)) { return; }
    integrate_body_position(body);
}

@compute @workgroup_size(64)
fn integrate_positions_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    integrate_positions_impl(gid);
}
@compute @workgroup_size(128)
fn integrate_positions_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    integrate_positions_impl(gid);
}
@compute @workgroup_size(256)
fn integrate_positions_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    integrate_positions_impl(gid);
}

fn integrate_velocity_state(pose : BodyPose, shape : BodyShape,
                            bodyMetadata : vec4<i32>,
                            motion : ptr<function, BodyMotion>) {
    if ((u32(bodyMetadata.w) & (BODY_ALIVE | BODY_AWAKE))
        != (BODY_ALIVE | BODY_AWAKE)) { return; }
    if (pose.position_invMass.w <= 1e-7) { return; }
    var linear = (*motion).linearVelocity_sleep.xyz
               + params.gravity_dt.xyz * params.gravity_dt.w;
    var angular = (*motion).angularVelocity_flags.xyz;
    let localAngular = quaternion_inverse_rotate(pose.orientation, angular);
    let inverseInertia = shape.invInertia_material.xyz;
    let inertia = vec3<f32>(
        select(0.0, 1.0 / inverseInertia.x, inverseInertia.x > 1e-9),
        select(0.0, 1.0 / inverseInertia.y, inverseInertia.y > 1e-9),
        select(0.0, 1.0 / inverseInertia.z, inverseInertia.z > 1e-9));
    let gyroscopicResidual = params.gravity_dt.w
        * cross(localAngular, inertia * localAngular);
    let correctedLocal = localAngular - inverseInertia * gyroscopicResidual;
    angular = quaternion_rotate(pose.orientation, correctedLocal);
    linear /= 1.0 + params.damping_slop.x * params.gravity_dt.w;
    angular /= 1.0 + params.damping_slop.y * params.gravity_dt.w;
    (*motion).linearVelocity_sleep = vec4<f32>(
        linear, (*motion).linearVelocity_sleep.w);
    (*motion).angularVelocity_flags = vec4<f32>(
        angular, (*motion).angularVelocity_flags.w);
}

fn integrate_position_state(motion : BodyMotion,
                            bodyMetadata : vec4<i32>,
                            pose : ptr<function, BodyPose>) {
    if ((u32(bodyMetadata.w) & (BODY_ALIVE | BODY_AWAKE))
        != (BODY_ALIVE | BODY_AWAKE)) { return; }
    if ((*pose).position_invMass.w <= 1e-7) { return; }
    (*pose).position_invMass = vec4<f32>(
        (*pose).position_invMass.xyz
            + motion.linearVelocity_sleep.xyz * params.gravity_dt.w,
        (*pose).position_invMass.w);
    let omega = vec4<f32>(motion.angularVelocity_flags.xyz, 0.0);
    var orientation = (*pose).orientation + 0.5 * params.gravity_dt.w
        * quaternion_multiply(omega, (*pose).orientation);
    let squared = dot(orientation, orientation);
    if (squared > 1e-12) { orientation *= inverseSqrt(squared); }
    else { orientation = vec4<f32>(0.0, 0.0, 0.0, 1.0); }
    (*pose).orientation = orientation;
}

fn store_state_velocities(motionA : ptr<function, BodyMotion>,
                          motionB : ptr<function, BodyMotion>,
                          velocities : VelocityPair) {
    (*motionA).linearVelocity_sleep = vec4<f32>(
        velocities.linearA, (*motionA).linearVelocity_sleep.w);
    (*motionA).angularVelocity_flags = vec4<f32>(
        velocities.angularA, (*motionA).angularVelocity_flags.w);
    (*motionB).linearVelocity_sleep = vec4<f32>(
        velocities.linearB, (*motionB).linearVelocity_sleep.w);
    (*motionB).angularVelocity_flags = vec4<f32>(
        velocities.angularB, (*motionB).angularVelocity_flags.w);
}

fn solve_small_islands_impl(gid : vec3<u32>) {
    let rank = gid.x;
    if (!contact_is_small_island(rank)) { return; }
    var manifold = manifolds[rank];
    let pair = manifold.pair;
    var poseA = poses[pair.keyHigh];
    var poseB = poses[pair.keyLow];
    var motionA = motions[pair.keyHigh];
    var motionB = motions[pair.keyLow];
    let shapeA = shapes[pair.keyHigh];
    let shapeB = shapes[pair.keyLow];
    var metadataA = metadata[pair.keyHigh];
    var metadataB = metadata[pair.keyLow];
    poseB.position_invMass = vec4<f32>(
        body_position_in_frame(pair.keyLow, pair.keyHigh),
        poseB.position_invMass.w);
    metadataB = vec4<i32>(metadataA.xyz, metadataB.w);
    let cache = constraintCaches[rank];
    for (var substep = 0u; substep < params.control.z; substep += 1u) {
        integrate_velocity_state(poseA, shapeA, metadataA, &motionA);
        integrate_velocity_state(poseB, shapeB, metadataB, &motionB);
        if (substep == 0u) {
            store_state_velocities(&motionA, &motionB, solve_contact_state(
                &manifold, cache, poseA, motionA, shapeA,
                poseB, motionB, shapeB, STAGE_WARM_START));
        }
        store_state_velocities(&motionA, &motionB, solve_contact_state(
            &manifold, cache, poseA, motionA, shapeA,
            poseB, motionB, shapeB, STAGE_BIASED));
        integrate_position_state(motionA, metadataA, &poseA);
        integrate_position_state(motionB, metadataB, &poseB);
        store_state_velocities(&motionA, &motionB, solve_contact_state(
            &manifold, cache, poseA, motionA, shapeA,
            poseB, motionB, shapeB, STAGE_RELAX));
    }
    store_state_velocities(&motionA, &motionB, solve_contact_state(
        &manifold, cache, poseA, motionA, shapeA,
        poseB, motionB, shapeB, STAGE_RESTITUTION));
    normalize_world_position(&poseA, &metadataA);
    normalize_world_position(&poseB, &metadataB);
    poses[pair.keyHigh] = poseA;
    poses[pair.keyLow] = poseB;
    metadata[pair.keyHigh] = metadataA;
    metadata[pair.keyLow] = metadataB;
    motions[pair.keyHigh] = motionA;
    motions[pair.keyLow] = motionB;
    manifolds[rank] = manifold;
}

fn solve_serial_contact(rank : u32, stage : u32) {
    if (!contact_is_active(rank)) { return; }
    let pair = manifolds[rank].pair;
    let velocities = solve_contact(rank, stage);
    store_contact_velocities(pair, velocities);
}

fn build_serial_dependency_levels(lane : u32, contactCount : u32) {
    for (var body = lane; body < SERIAL_WORLD_BODY_CAPACITY; body += 256u) {
        serialBodyNextLevel[body] = 0u;
    }
    for (var level = lane; level < SERIAL_LEVEL_CAPACITY; level += 256u) {
        serialLevelCounts[level] = 0u;
    }
    workgroupBarrier();

    if (lane == 0u) {
        for (var rank = 0u; rank < contactCount; rank += 1u) {
            var cache = constraintCaches[rank];
            var level = SENTINEL;
            if (contact_is_active(rank)) {
                let pair = manifolds[rank].pair;
                level = max(serialBodyNextLevel[pair.keyHigh],
                            serialBodyNextLevel[pair.keyLow]);
                let nextLevel = level + 1u;
                serialBodyNextLevel[pair.keyHigh] = nextLevel;
                serialBodyNextLevel[pair.keyLow] = nextLevel;
                serialLevelCounts[level] += 1u;
            }
            // softness.w is intentionally unused by constraint evaluation.
            cache.softness.w = bitcast<f32>(level);
            constraintCaches[rank] = cache;
        }

        var offset = 0u;
        for (var level = 0u; level < SERIAL_LEVEL_CAPACITY; level += 1u) {
            serialLevelOffsets[level] = offset;
            offset += serialLevelCounts[level];
            serialLevelCounts[level] = 0u;
        }
        // Compact each level once. tangentMass.w is also unused by constraint
        // evaluation, so the existing cache is sufficient scratch storage.
        for (var rank = 0u; rank < contactCount; rank += 1u) {
            if (!contact_is_active(rank)) { continue; }
            let level = bitcast<u32>(constraintCaches[rank].softness.w);
            let packed = serialLevelOffsets[level]
                       + serialLevelCounts[level];
            constraintCaches[packed].tangentMass.w = bitcast<f32>(rank);
            serialLevelCounts[level] += 1u;
        }
    }
    storageBarrier();
    workgroupBarrier();
}

fn solve_serial_stage(lane : u32, contactCount : u32, stage : u32,
                      dependencyLevels : bool) {
    if (!dependencyLevels) {
        if (lane == 0u) {
            for (var rank = 0u; rank < contactCount; rank += 1u) {
                solve_serial_contact(rank, stage);
            }
        }
        return;
    }

    // Pairs arrive in lexicographic (minimum, maximum) order. For n bodies,
    // the complete graph reaches level 2n-4; removing pairs can only lower
    // the per-body next levels. A uniform-buffer bound keeps every lane's
    // barriers in uniform control flow, as required by browser WebGPU.
    for (var level = 0u; level < 2u * params.capacities.x; level += 1u) {
        let offset = serialLevelOffsets[level];
        let count = serialLevelCounts[level];
        for (var index = lane; index < count; index += 256u) {
            let rank = bitcast<u32>(
                constraintCaches[offset + index].tangentMass.w);
            solve_serial_contact(rank, stage);
        }
        storageBarrier();
        workgroupBarrier();
    }
}

// Small GPU worlds are latency-bound, not throughput-bound. Run the exact
// Soft Step contact sequence in one dispatch, while distributing independent
// per-body integration across one workgroup.
@compute @workgroup_size(256)
fn solve_serial_world(@builtin(local_invocation_id) lid : vec3<u32>) {
    let lane = lid.x;
    let contactCount = active_contact_count();
    if (lane == 0u) {
        for (var index = 0u; index < 39u; index += 1u) {
            atomicStore(&solverTelemetry[index], 0u);
        }
        for (var index = 42u; index < 45u; index += 1u) {
            atomicStore(&solverTelemetry[index], 0u);
        }
    }
    storageBarrier();
    workgroupBarrier();

    // Constraint preparation touches only the selected contact and cache.
    // Spread those independent records across the workgroup while preserving
    // the rank-ordered contact solve below.
    for (var rank = lane; rank < contactCount; rank += 256u) {
        if (contact_is_active(rank)) {
            atomicAdd(&solverTelemetry[32], 1u);
            prepare_constraints_impl(vec3<u32>(rank, 0u, 0u));
        }
    }
    storageBarrier();
    workgroupBarrier();

    let dependencyLevels =
        contactCount >= SERIAL_LEVEL_CONTACT_THRESHOLD;
    if (dependencyLevels) {
        build_serial_dependency_levels(lane, contactCount);
    }

    // Detailed classification is diagnostic-only. Every body/contact owns an
    // independent lane; integer max/sum reductions are exact and commutative.
    if (contactCount <= 256u) {
        for (var body = lane; body < params.capacities.x; body += 256u) {
            var degree = 0u;
            for (var rank = 0u; rank < contactCount; rank += 1u) {
                if (!contact_is_active(rank)) { continue; }
                let pair = manifolds[rank].pair;
                degree += select(0u, 1u,
                    pair.keyHigh == body || pair.keyLow == body);
            }
            atomicMax(&solverTelemetry[36], degree);
        }
        for (var rank = lane; rank < contactCount; rank += 256u) {
            if (!contact_is_active(rank)) { continue; }
            let pair = manifolds[rank].pair;
            var degreeA = 0u;
            var degreeB = 0u;
            for (var other = 0u; other < contactCount; other += 1u) {
                if (!contact_is_active(other)) { continue; }
                let candidate = manifolds[other].pair;
                degreeA += select(0u, 1u,
                    candidate.keyHigh == pair.keyHigh
                        || candidate.keyLow == pair.keyHigh);
                degreeB += select(0u, 1u,
                    candidate.keyHigh == pair.keyLow
                        || candidate.keyLow == pair.keyLow);
            }
            atomicAdd(&solverTelemetry[43], select(
                0u, 1u, degreeA == 1u && degreeB == 1u));
        }
    }
    storageBarrier();
    workgroupBarrier();

    for (var substep = 0u; substep < params.control.z; substep += 1u) {
        for (var body = lane; body < params.capacities.x; body += 256u) {
            integrate_body_velocity(body);
        }
        storageBarrier();
        workgroupBarrier();
        if (substep == 0u) {
            solve_serial_stage(lane, contactCount, STAGE_WARM_START,
                               dependencyLevels);
        }
        solve_serial_stage(lane, contactCount, STAGE_BIASED,
                           dependencyLevels);
        storageBarrier();
        workgroupBarrier();
        let finalSubstep = substep + 1u == params.control.z;
        for (var body = lane; body < params.capacities.x; body += 256u) {
            integrate_body_position_serial(body, finalSubstep);
        }
        storageBarrier();
        workgroupBarrier();
        solve_serial_stage(lane, contactCount, STAGE_RELAX,
                           dependencyLevels);
        storageBarrier();
        workgroupBarrier();
    }
    solve_serial_stage(lane, contactCount, STAGE_RESTITUTION,
                       dependencyLevels);
    if (lane == 0u) {
        let activeCount = atomicLoad(&solverTelemetry[32]);
        let maximumDegree = atomicLoad(&solverTelemetry[36]);
        let smallIslandCount = atomicLoad(&solverTelemetry[43]);

        atomicStore(&solverTelemetry[0], activeCount - smallIslandCount);
        atomicStore(&solverTelemetry[32], activeCount);
        atomicStore(&solverTelemetry[33], activeCount - smallIslandCount);
        atomicStore(&solverTelemetry[36], maximumDegree);
        atomicStore(&solverTelemetry[38], params.control.y);
        atomicMax(&solverTelemetry[39], activeCount);
        atomicStore(&solverTelemetry[42], select(
            0u, 1u, narrowTelemetry[10] > params.capacities.y));
        atomicStore(&solverTelemetry[43], smallIslandCount);
        atomicStore(&solverTelemetry[44], smallIslandCount * 2u);
        atomicStore(&solverTelemetry[45], 1u);
        atomicAdd(&solverTelemetry[41], 1u);
    }
}

@compute @workgroup_size(64)
fn solve_small_islands_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    solve_small_islands_impl(gid);
}
@compute @workgroup_size(128)
fn solve_small_islands_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    solve_small_islands_impl(gid);
}
@compute @workgroup_size(256)
fn solve_small_islands_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    solve_small_islands_impl(gid);
}

@compute @workgroup_size(1)
fn finish_solver_tick(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    atomicAdd(&solverTelemetry[41], 1u);
}
