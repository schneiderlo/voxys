const BODY_ALIVE : u32 = 1u << 20u;
const BODY_AWAKE : u32 = 1u << 21u;
const TERRAIN_CONTACT_MASK : u32 = 0xfu << 27u;
const SENTINEL : u32 = 0xffffffffu;
const CELL_MASK : u32 = 0x1fffffu;
const CELL_BIAS : i32 = 1048576;

struct BodyPose {
    position_invMass : vec4<f32>,
    orientation : vec4<f32>,
};

struct BodyMotion {
    linearVelocity_sleep : vec4<f32>,
    angularVelocity_flags : vec4<f32>,
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

struct IslandRecord {
    rootBody : u32,
    firstBodyRecord : u32,
    bodyCount : u32,
    state : u32,
};

struct IslandScratch {
    bodyCount : atomic<u32>,
    awakeCount : atomic<u32>,
    qualifies : atomic<u32>,
    disturbed : atomic<u32>,
};

struct IslandPersistent {
    sleepTicks : u32,
    state : u32,
    previousBodyCount : u32,
    reserved : u32,
};

struct BodyPersistent {
    previousRoot : u32,
    previousSleeping : u32,
    reserved0 : u32,
    reserved1 : u32,
};

struct SleepingCellRange {
    keyLow : u32,
    keyHigh : u32,
    firstEntry : u32,
    entryCount : u32,
};

struct IslandEvent {
    rootBody : u32,
    eventType : u32,
    tick : u32,
    bodyCount : u32,
};

struct IslandParams {
    capacities : vec4<u32>,
    control : vec4<u32>,
    thresholds : vec4<f32>,
};

@group(0) @binding(0) var<storage, read> poses : array<BodyPose>;
@group(0) @binding(1) var<storage, read_write> motions : array<BodyMotion>;
@group(0) @binding(3) var<storage, read_write> metadata : array<vec4<i32>>;
@group(0) @binding(4) var<storage, read> manifolds : array<ContactManifold>;
@group(0) @binding(5) var<storage, read> narrowTelemetry : array<u32>;
@group(0) @binding(6) var<storage, read_write> bodyRoots : array<atomic<u32>>;
@group(0) @binding(7) var<storage, read_write> bodyRecords : array<KeyValue>;
@group(0) @binding(8) var<storage, read_write> sortedBodyRecords : array<KeyValue>;
@group(0) @binding(9) var<storage, read_write> telemetry : array<atomic<u32>>;
@group(0) @binding(10) var<uniform> params : IslandParams;
@group(0) @binding(11) var<storage, read_write> islandRecords : array<IslandRecord>;
@group(0) @binding(12) var<storage, read_write> islandScratch : array<IslandScratch>;
@group(0) @binding(13) var<storage, read_write> islandPersistent :
    array<IslandPersistent>;
@group(0) @binding(14) var<storage, read_write> bodyPersistent :
    array<BodyPersistent>;
@group(0) @binding(15) var<storage, read_write> sleepingGrid : array<KeyValue>;
@group(0) @binding(16) var<storage, read_write> sortedSleepingGrid : array<KeyValue>;
@group(0) @binding(17) var<storage, read_write> sleepingCellRanges :
    array<SleepingCellRange>;
@group(0) @binding(18) var<storage, read_write> islandEvents : array<IslandEvent>;
@group(0) @binding(19) var<storage, read_write> rangePredicates : array<u32>;
@group(0) @binding(20) var<storage, read> rangeIndices : array<u32>;
@group(0) @binding(21) var<storage, read_write> pendingEvents : array<IslandEvent>;
@group(0) @binding(22) var<storage, read_write> compactedSleepingGrid :
    array<KeyValue>;
@group(0) @binding(23) var<storage, read_write> unionConvergence :
    array<atomic<u32>>;

// The compact island path is selected only when bodyCapacity <= 1,024.
// Sentinel padding gives every supported capacity the same fixed network.
var<workgroup> smallWorldSortRecords : array<KeyValue, 1024>;

fn sentinel_record() -> KeyValue {
    return KeyValue(SENTINEL, SENTINEL, SENTINEL, SENTINEL);
}

fn body_record_less(a : KeyValue, b : KeyValue) -> bool {
    return a.keyHigh < b.keyHigh
        || (a.keyHigh == b.keyHigh && a.keyLow < b.keyLow);
}

fn store_sort_dispatch(base : u32, count : u32) {
    atomicStore(&telemetry[base],
        (count + params.capacities.w - 1u) / params.capacities.w);
    atomicStore(&telemetry[base + 1u], 1u);
    atomicStore(&telemetry[base + 2u], 1u);
    atomicStore(&telemetry[base + 3u], select(0u, 1u, count != 0u));
    atomicStore(&telemetry[base + 4u], 1u);
    atomicStore(&telemetry[base + 5u], 1u);
}

fn body_is_alive(body : u32) -> bool {
    return body < params.capacities.x
        && (u32(metadata[body].w) & BODY_ALIVE) != 0u;
}

fn body_is_slow(body : u32) -> bool {
    let motion = motions[body];
    let terrainSupported =
        (u32(metadata[body].w) & TERRAIN_CONTACT_MASK) != 0u;
    // The static solver stores constraint-free linear activity in w. Its xyz
    // velocity can contain penetration bias even when the pose is stationary.
    let linearActivitySquared = select(
        dot(motion.linearVelocity_sleep.xyz,
            motion.linearVelocity_sleep.xyz),
        motion.linearVelocity_sleep.w,
        terrainSupported);
    return linearActivitySquared <= params.thresholds.x
        && dot(motion.angularVelocity_flags.xyz,
               motion.angularVelocity_flags.xyz) <= params.thresholds.y;
}

fn active_contact_count() -> u32 {
    return min(narrowTelemetry[24], params.capacities.y);
}

@compute @workgroup_size(1)
fn prepare_union_dispatch(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    let contactCount = active_contact_count();
    let hasContacts = contactCount != 0u;
    atomicStore(&telemetry[12], select(0u, params.control.x, hasContacts));
    atomicStore(&unionConvergence[0], select(0u, 1u, hasContacts));
    atomicStore(&telemetry[21], 0u);
    atomicStore(&telemetry[25],
        (contactCount + params.capacities.w - 1u) / params.capacities.w);
    atomicStore(&telemetry[26], 1u);
    atomicStore(&telemetry[27], 1u);
    atomicStore(&telemetry[28], select(0u,
        (params.capacities.x + params.capacities.w - 1u)
            / params.capacities.w,
        hasContacts));
    atomicStore(&telemetry[29], 1u);
    atomicStore(&telemetry[30], 1u);
    atomicStore(&telemetry[31], select(0u, 1u, hasContacts));
    atomicStore(&telemetry[32], 1u);
    atomicStore(&telemetry[33], 1u);
    atomicStore(&telemetry[34], select(0u, params.capacities.x, hasContacts));
}

@compute @workgroup_size(1)
fn prepare_union_round(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    let roundActive = atomicExchange(&unionConvergence[0], 0u) != 0u;
    if (roundActive) {
        atomicAdd(&telemetry[21], 1u);
        return;
    }
    atomicStore(&telemetry[25], 0u);
    atomicStore(&telemetry[28], 0u);
}

fn reset_impl(gid : vec3<u32>) {
    let index = gid.x;
    let firstTick = atomicLoad(&telemetry[13]) == 0u;
    if (index < params.capacities.x) {
        atomicStore(&bodyRoots[index], select(SENTINEL, index,
            body_is_alive(index)));
        bodyRecords[index] = sentinel_record();
        atomicStore(&islandScratch[index].bodyCount, 0u);
        atomicStore(&islandScratch[index].awakeCount, 0u);
        atomicStore(&islandScratch[index].qualifies, params.control.y);
        atomicStore(&islandScratch[index].disturbed, 0u);
        if (firstTick) {
            islandPersistent[index] = IslandPersistent(0u, 0u, 0u, 0u);
            bodyPersistent[index] = BodyPersistent(SENTINEL, 0u, 0u, 0u);
        }
    }
    if (index < params.capacities.z) {
        islandEvents[index] = IslandEvent(SENTINEL, 0u, 0u, 0u);
    }
    if (index < 13u || index == 17u || index == 18u) {
        atomicStore(&telemetry[index], 0u);
    }
}

@compute @workgroup_size(64)
fn reset_islands_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    reset_impl(gid);
}
@compute @workgroup_size(128)
fn reset_islands_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    reset_impl(gid);
}
@compute @workgroup_size(256)
fn reset_islands_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    reset_impl(gid);
}

fn union_contacts_impl(gid : vec3<u32>) {
    let rank = gid.x;
    if (rank >= active_contact_count() || manifolds[rank].state.x == 0u) {
        return;
    }
    let pair = manifolds[rank].pair;
    if (!body_is_alive(pair.keyHigh) || !body_is_alive(pair.keyLow)) { return; }
    // Fixed scenery supports contacts without coupling independent dynamic
    // islands. Keep its singleton sleep/grid record for broad-phase discovery.
    if (poses[pair.keyHigh].position_invMass.w == 0.0
        || poses[pair.keyLow].position_invMass.w == 0.0) { return; }
    let rootA = atomicLoad(&bodyRoots[pair.keyHigh]);
    let rootB = atomicLoad(&bodyRoots[pair.keyLow]);
    if (rootA == SENTINEL || rootB == SENTINEL || rootA == rootB) { return; }
    let lower = min(rootA, rootB);
    let previous = atomicMin(&bodyRoots[max(rootA, rootB)], lower);
    if (previous > lower) {
        atomicStore(&unionConvergence[0], 1u);
    }
}

@compute @workgroup_size(64)
fn union_contacts_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    union_contacts_impl(gid);
}
@compute @workgroup_size(128)
fn union_contacts_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    union_contacts_impl(gid);
}
@compute @workgroup_size(256)
fn union_contacts_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    union_contacts_impl(gid);
}

fn compress_roots_impl(gid : vec3<u32>) {
    let body = gid.x;
    if (!body_is_alive(body)) { return; }
    let root = atomicLoad(&bodyRoots[body]);
    if (root != SENTINEL) {
        let parent = atomicLoad(&bodyRoots[root]);
        atomicStore(&bodyRoots[body], parent);
        if (parent != root) {
            atomicStore(&unionConvergence[0], 1u);
        }
    }
}

@compute @workgroup_size(64)
fn compress_roots_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    compress_roots_impl(gid);
}
@compute @workgroup_size(128)
fn compress_roots_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    compress_roots_impl(gid);
}
@compute @workgroup_size(256)
fn compress_roots_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    compress_roots_impl(gid);
}

fn canonical_root(body : u32) -> u32 {
    var root = atomicLoad(&bodyRoots[body]);
    for (var round = 0u; round < params.control.x; round += 1u) {
        if (root == SENTINEL) { break; }
        let parent = atomicLoad(&bodyRoots[root]);
        if (parent == root) { break; }
        root = parent;
    }
    return root;
}

fn build_body_records_impl(gid : vec3<u32>) {
    let body = gid.x;
    if (body >= params.capacities.x) { return; }
    bodyRecords[body] = sentinel_record();
    sortedBodyRecords[body] = sentinel_record();
    if (!body_is_alive(body)) { return; }
    let root = canonical_root(body);
    atomicStore(&bodyRoots[body], root);
    bodyRecords[body] = KeyValue(body, root, body, body);
    sortedBodyRecords[body] = bodyRecords[body];
    if (root == SENTINEL || atomicLoad(&bodyRoots[root]) != root) {
        atomicAdd(&telemetry[11], 1u);
    }
}

@compute @workgroup_size(64)
fn build_body_records_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    build_body_records_impl(gid);
}
@compute @workgroup_size(128)
fn build_body_records_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    build_body_records_impl(gid);
}
@compute @workgroup_size(256)
fn build_body_records_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    build_body_records_impl(gid);
}

fn mark_island_range_starts_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= params.capacities.x) { return; }
    var start = 0u;
    let current = sortedBodyRecords[index];
    if (current.keyHigh != SENTINEL) {
        start = select(0u, 1u, index == 0u
            || sortedBodyRecords[index - select(0u, 1u, index != 0u)].keyHigh
                != current.keyHigh);
    }
    rangePredicates[index] = start;
}

@compute @workgroup_size(64)
fn mark_island_range_starts_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_island_range_starts_impl(gid);
}
@compute @workgroup_size(128)
fn mark_island_range_starts_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_island_range_starts_impl(gid);
}
@compute @workgroup_size(256)
fn mark_island_range_starts_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_island_range_starts_impl(gid);
}

@compute @workgroup_size(1)
fn finalize_island_count(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    var count = 0u;
    if (params.capacities.x != 0u) {
        let last = params.capacities.x - 1u;
        count = rangeIndices[last] + rangePredicates[last];
    }
    atomicStore(&telemetry[0], count);
    atomicMax(&telemetry[14], count);
}

fn scatter_island_range_starts_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= params.capacities.x || rangePredicates[index] == 0u) {
        return;
    }
    let record = sortedBodyRecords[index];
    islandRecords[rangeIndices[index]] = IslandRecord(
        record.keyHigh, index, 0u, 0u);
}

@compute @workgroup_size(64)
fn scatter_island_range_starts_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_island_range_starts_impl(gid);
}
@compute @workgroup_size(128)
fn scatter_island_range_starts_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_island_range_starts_impl(gid);
}
@compute @workgroup_size(256)
fn scatter_island_range_starts_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_island_range_starts_impl(gid);
}

fn scatter_island_range_ends_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= params.capacities.x
        || sortedBodyRecords[index].keyHigh == SENTINEL) { return; }
    let end = index + 1u == params.capacities.x
        || sortedBodyRecords[index + 1u].keyHigh
            != sortedBodyRecords[index].keyHigh;
    if (!end) { return; }
    var island = rangeIndices[index];
    if (rangePredicates[index] == 0u) { island -= 1u; }
    let count = index + 1u - islandRecords[island].firstBodyRecord;
    islandRecords[island].bodyCount = count;
    atomicMax(&telemetry[5], count);
}

@compute @workgroup_size(64)
fn scatter_island_range_ends_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_island_range_ends_impl(gid);
}
@compute @workgroup_size(128)
fn scatter_island_range_ends_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_island_range_ends_impl(gid);
}
@compute @workgroup_size(256)
fn scatter_island_range_ends_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_island_range_ends_impl(gid);
}

fn classify_bodies_impl(gid : vec3<u32>) {
    let body = gid.x;
    if (!body_is_alive(body)) { return; }
    let root = atomicLoad(&bodyRoots[body]);
    if (root == SENTINEL) { return; }
    atomicAdd(&islandScratch[root].bodyCount, 1u);
    if ((u32(metadata[body].w) & BODY_AWAKE) != 0u) {
        atomicAdd(&islandScratch[root].awakeCount, 1u);
    }
    let slow = body_is_slow(body);
    var persistent = bodyPersistent[body];
    persistent.reserved1 = select(
        0u, min(persistent.reserved1 + 1u, params.control.y),
        slow && persistent.previousRoot != SENTINEL);
    bodyPersistent[body] = persistent;
    atomicMin(&islandScratch[root].qualifies, persistent.reserved1);
    if (bodyPersistent[body].previousSleeping != 0u) {
        atomicOr(&islandScratch[root].disturbed, 2u);
    }
}

@compute @workgroup_size(64)
fn classify_bodies_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    classify_bodies_impl(gid);
}
@compute @workgroup_size(128)
fn classify_bodies_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    classify_bodies_impl(gid);
}
@compute @workgroup_size(256)
fn classify_bodies_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    classify_bodies_impl(gid);
}

fn decide_islands_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= params.capacities.x) { return; }
    rangePredicates[index] = 0u;
    let islandCount = atomicLoad(&telemetry[0]);
    if (index >= islandCount) { return; }
    let tick = atomicLoad(&telemetry[13]) + 1u;
    var island = islandRecords[index];
    let root = island.rootBody;
    let bodyCount = atomicLoad(&islandScratch[root].bodyCount);
    let awakeCount = atomicLoad(&islandScratch[root].awakeCount);
    let quietTicks = atomicLoad(&islandScratch[root].qualifies);
    let disturbanceFlags = atomicLoad(&islandScratch[root].disturbed);
    let wasSleeping = (disturbanceFlags & 2u) != 0u;
    let previousState = select(islandPersistent[root].state, 1u,
        islandPersistent[root].previousBodyCount == 0u
            && wasSleeping);
    var nextState = previousState;
    if (wasSleeping && awakeCount != 0u) {
        nextState = 0u;
    } else if (awakeCount == 0u) {
        nextState = 1u;
    } else {
        nextState = select(0u, 1u, quietTicks >= params.control.y);
    }
    islandPersistent[root] = IslandPersistent(
        quietTicks, nextState, bodyCount, 0u);
    island.state = nextState;
    islandRecords[index] = island;
    if (nextState != previousState) {
        let eventType = select(2u, 1u, nextState != 0u);
        pendingEvents[index] = IslandEvent(root, eventType, tick, bodyCount);
        rangePredicates[index] = 1u;
        if (nextState != 0u) {
            atomicAdd(&telemetry[6], 1u);
        } else {
            atomicAdd(&telemetry[7], 1u);
        }
    }
    if (nextState != 0u) {
        atomicAdd(&telemetry[2], 1u);
        atomicAdd(&telemetry[4], bodyCount);
    } else {
        atomicAdd(&telemetry[1], 1u);
        atomicAdd(&telemetry[3], bodyCount);
    }
}

@compute @workgroup_size(64)
fn decide_islands_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    decide_islands_impl(gid);
}
@compute @workgroup_size(128)
fn decide_islands_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    decide_islands_impl(gid);
}
@compute @workgroup_size(256)
fn decide_islands_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    decide_islands_impl(gid);
}

@compute @workgroup_size(1)
fn finalize_island_events(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    var eventCount = 0u;
    if (params.capacities.x != 0u) {
        let last = params.capacities.x - 1u;
        eventCount = rangeIndices[last] + rangePredicates[last];
    }
    atomicStore(&telemetry[8], min(eventCount, params.capacities.z));
    atomicStore(&telemetry[17], select(0u, 1u,
        eventCount > params.capacities.z));
    atomicStore(&telemetry[13], atomicLoad(&telemetry[13]) + 1u);
    atomicMax(&telemetry[15], atomicLoad(&telemetry[4]));
    atomicMax(&telemetry[19], min(eventCount, params.capacities.z));
}

fn scatter_island_events_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= params.capacities.x || rangePredicates[index] == 0u) {
        return;
    }
    let output = rangeIndices[index];
    if (output < params.capacities.z) {
        islandEvents[output] = pendingEvents[index];
    }
}

@compute @workgroup_size(64)
fn scatter_island_events_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_island_events_impl(gid);
}
@compute @workgroup_size(128)
fn scatter_island_events_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_island_events_impl(gid);
}
@compute @workgroup_size(256)
fn scatter_island_events_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_island_events_impl(gid);
}

fn coordinate_is_encodable(cell : vec3<i32>) -> bool {
    return all(cell >= vec3<i32>(-CELL_BIAS))
        && all(cell < vec3<i32>(CELL_BIAS));
}

fn encode_cell(cell : vec3<i32>) -> vec2<u32> {
    let x = u32(cell.x + CELL_BIAS) & CELL_MASK;
    let y = u32(cell.y + CELL_BIAS) & CELL_MASK;
    let z = u32(cell.z + CELL_BIAS) & CELL_MASK;
    return vec2<u32>(x | (y << 21u), (y >> 11u) | (z << 10u));
}

fn sleeping_cell(body : u32, valid : ptr<function, bool>) -> vec3<i32> {
    let localCell = vec3<i32>(floor(
        poses[body].position_invMass.xyz / params.thresholds.z));
    let sectors = metadata[body].xyz;
    if (all(sectors == vec3<i32>(0))) {
        (*valid) = coordinate_is_encodable(localCell);
        return localCell;
    }
    let cellsPerSector = i32(params.thresholds.w);
    if (cellsPerSector <= 0) {
        (*valid) = false;
        return vec3<i32>(0);
    }
    // Sleeping-grid keys are toroidal buckets. Restricting this arithmetic to
    // the low 21 bits keeps it defined for every signed i32 sector. Exact body
    // positions remain in metadata and poses; key aliases only share a range.
    let cellBits = bitcast<vec3<u32>>(sectors)
        * vec3<u32>(u32(cellsPerSector))
        + bitcast<vec3<u32>>(localCell)
        + vec3<u32>(u32(CELL_BIAS));
    let encoded = cellBits & vec3<u32>(CELL_MASK);
    (*valid) = true;
    return vec3<i32>(encoded) - vec3<i32>(CELL_BIAS);
}

fn apply_states_impl(gid : vec3<u32>) {
    let body = gid.x;
    if (body >= params.capacities.x) { return; }
    if (!body_is_alive(body)) {
        sleepingGrid[body] = sentinel_record();
        bodyPersistent[body] = BodyPersistent(SENTINEL, 0u, 0u, 0u);
        return;
    }
    let root = atomicLoad(&bodyRoots[body]);
    let sleeping = islandPersistent[root].state != 0u;
    var bodyMetadata = metadata[body];
    var packedMetadata = u32(bodyMetadata.w);
    if (sleeping) {
        packedMetadata &= ~BODY_AWAKE;
        motions[body].linearVelocity_sleep = vec4<f32>(0.0);
        motions[body].angularVelocity_flags = vec4<f32>(0.0);
        if (bodyPersistent[body].previousSleeping == 0u) {
            var validCell = false;
            let cell = sleeping_cell(body, &validCell);
            if (validCell) {
                let key = encode_cell(cell);
                sleepingGrid[body] = KeyValue(key.x, key.y, body, body);
            } else {
                sleepingGrid[body] = sentinel_record();
                atomicStore(&telemetry[18], 1u);
            }
        }
    } else {
        packedMetadata |= BODY_AWAKE;
        sleepingGrid[body] = sentinel_record();
    }
    bodyMetadata.w = bitcast<i32>(packedMetadata);
    metadata[body] = bodyMetadata;
    let persistent = bodyPersistent[body];
    let quietTicks = select(persistent.reserved1, 0u,
        persistent.previousSleeping != 0u && !sleeping);
    bodyPersistent[body] = BodyPersistent(
        root, select(0u, 1u, sleeping), 0u,
        select(quietTicks, params.control.y, sleeping));
}

@compute @workgroup_size(64)
fn apply_states_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    apply_states_impl(gid);
}
@compute @workgroup_size(128)
fn apply_states_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    apply_states_impl(gid);
}
@compute @workgroup_size(256)
fn apply_states_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    apply_states_impl(gid);
}

fn mark_sleeping_entries_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= params.capacities.x) { return; }
    rangePredicates[index] = select(0u, 1u,
        sleepingGrid[index].keyHigh != SENTINEL);
}

@compute @workgroup_size(64)
fn mark_sleeping_entries_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_sleeping_entries_impl(gid);
}
@compute @workgroup_size(128)
fn mark_sleeping_entries_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_sleeping_entries_impl(gid);
}
@compute @workgroup_size(256)
fn mark_sleeping_entries_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_sleeping_entries_impl(gid);
}

fn compact_sleeping_entries_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= params.capacities.x || rangePredicates[index] == 0u) {
        return;
    }
    compactedSleepingGrid[rangeIndices[index]] = sleepingGrid[index];
}

@compute @workgroup_size(64)
fn compact_sleeping_entries_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    compact_sleeping_entries_impl(gid);
}
@compute @workgroup_size(128)
fn compact_sleeping_entries_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    compact_sleeping_entries_impl(gid);
}
@compute @workgroup_size(256)
fn compact_sleeping_entries_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    compact_sleeping_entries_impl(gid);
}

@compute @workgroup_size(1)
fn finalize_sleeping_entry_count(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    var count = 0u;
    if (params.capacities.x != 0u) {
        let last = params.capacities.x - 1u;
        count = rangeIndices[last] + rangePredicates[last];
    }
    atomicStore(&telemetry[9], count);
    store_sort_dispatch(19u, count);
}

fn mark_sleeping_range_starts_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= params.capacities.x) { return; }
    sleepingCellRanges[index] = SleepingCellRange(
        SENTINEL, SENTINEL, 0u, 0u);
    var start = 0u;
    let current = sortedSleepingGrid[index];
    if (index < atomicLoad(&telemetry[9]) && current.keyHigh != SENTINEL) {
        if (index == 0u) {
            start = 1u;
        } else {
            let previous = sortedSleepingGrid[index - 1u];
            start = select(0u, 1u, current.keyLow != previous.keyLow
                || current.keyHigh != previous.keyHigh);
        }
    }
    rangePredicates[index] = start;
}

@compute @workgroup_size(64)
fn mark_sleeping_range_starts_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_sleeping_range_starts_impl(gid);
}
@compute @workgroup_size(128)
fn mark_sleeping_range_starts_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_sleeping_range_starts_impl(gid);
}
@compute @workgroup_size(256)
fn mark_sleeping_range_starts_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_sleeping_range_starts_impl(gid);
}

@compute @workgroup_size(1)
fn finalize_sleeping_ranges(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    var rangeCount = 0u;
    let entryCount = atomicLoad(&telemetry[9]);
    if (entryCount != 0u) {
        let last = entryCount - 1u;
        rangeCount = rangeIndices[last] + rangePredicates[last];
    }
    atomicStore(&telemetry[10], rangeCount);
    atomicMax(&telemetry[16], atomicLoad(&telemetry[9]));
}

fn scatter_sleeping_range_starts_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= params.capacities.x || rangePredicates[index] == 0u) {
        return;
    }
    let record = sortedSleepingGrid[index];
    sleepingCellRanges[rangeIndices[index]] = SleepingCellRange(
        record.keyLow, record.keyHigh, index, 0u);
}

@compute @workgroup_size(64)
fn scatter_sleeping_range_starts_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_sleeping_range_starts_impl(gid);
}
@compute @workgroup_size(128)
fn scatter_sleeping_range_starts_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_sleeping_range_starts_impl(gid);
}
@compute @workgroup_size(256)
fn scatter_sleeping_range_starts_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_sleeping_range_starts_impl(gid);
}

fn scatter_sleeping_range_ends_impl(gid : vec3<u32>) {
    let index = gid.x;
    let entryCount = atomicLoad(&telemetry[9]);
    if (index >= entryCount
        || sortedSleepingGrid[index].keyHigh == SENTINEL) { return; }
    var end = index + 1u == entryCount;
    if (!end) {
        let current = sortedSleepingGrid[index];
        let next = sortedSleepingGrid[index + 1u];
        end = current.keyLow != next.keyLow || current.keyHigh != next.keyHigh;
    }
    if (!end) { return; }
    var range = rangeIndices[index];
    if (rangePredicates[index] == 0u) { range -= 1u; }
    sleepingCellRanges[range].entryCount = index + 1u
        - sleepingCellRanges[range].firstEntry;
}

@compute @workgroup_size(64)
fn scatter_sleeping_range_ends_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_sleeping_range_ends_impl(gid);
}
@compute @workgroup_size(128)
fn scatter_sleeping_range_ends_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_sleeping_range_ends_impl(gid);
}
@compute @workgroup_size(256)
fn scatter_sleeping_range_ends_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_sleeping_range_ends_impl(gid);
}

fn small_world_root(body : u32) -> u32 {
    var root = atomicLoad(&bodyRoots[body]);
    for (var step = 0u; step < params.capacities.x; step += 1u) {
        if (root == SENTINEL) { return SENTINEL; }
        let parent = atomicLoad(&bodyRoots[root]);
        if (parent == root) { return root; }
        root = parent;
    }
    return root;
}

// One workgroup keeps the latency advantage of the compact path while
// spreading medium-world work across 256 lanes. Storage barriers make each
// phase visible without paying for another WebGPU dispatch.
fn small_world_barrier() {
    storageBarrier();
    workgroupBarrier();
}

@compute @workgroup_size(256)
fn small_world_build(@builtin(global_invocation_id) gid : vec3<u32>) {
    let lane = gid.x;
    if (lane < 13u) {
        atomicStore(&telemetry[lane], 0u);
    }
    if (lane == 0u) {
        atomicStore(&telemetry[17], 0u);
        atomicStore(&telemetry[18], 0u);
        atomicStore(&telemetry[21], 0u);
    }

    for (var body = lane; body < params.capacities.x; body += 256u) {
        atomicStore(&bodyRoots[body], select(
            SENTINEL, body, body_is_alive(body)));
        sortedBodyRecords[body] = sentinel_record();
    }
    small_world_barrier();

    let contactCount = active_contact_count();
    // With no dynamic contacts every live body is already its own canonical
    // root. Union and path compression are identities, so defer all rounds.
    let unionRounds = select(0u, params.control.x, contactCount != 0u);
    // Telemetry word 35 is internal convergence scratch and is restored below.
    for (var round = 0u; round < unionRounds; round += 1u) {
        if (lane == 0u) {
            atomicStore(&telemetry[35], 0u);
        }
        workgroupBarrier();
        for (var rank = lane; rank < contactCount; rank += 256u) {
            if (manifolds[rank].state.x == 0u) { continue; }
            let pair = manifolds[rank].pair;
            if (!body_is_alive(pair.keyHigh) || !body_is_alive(pair.keyLow)) {
                continue;
            }
            if (poses[pair.keyHigh].position_invMass.w == 0.0
                || poses[pair.keyLow].position_invMass.w == 0.0) { continue; }
            let rootA = small_world_root(pair.keyHigh);
            let rootB = small_world_root(pair.keyLow);
            if (rootA == SENTINEL || rootB == SENTINEL || rootA == rootB) {
                continue;
            }
            let higher = max(rootA, rootB);
            let lower = min(rootA, rootB);
            let previous = atomicMin(&bodyRoots[higher], lower);
            if (previous > lower) {
                atomicStore(&telemetry[35], 1u);
            }
        }
        small_world_barrier();
        for (var body = lane; body < params.capacities.x; body += 256u) {
            if (body_is_alive(body)) {
                let previous = atomicLoad(&bodyRoots[body]);
                let root = small_world_root(body);
                atomicStore(&bodyRoots[body], root);
                if (root != previous) {
                    atomicStore(&telemetry[35], 1u);
                }
            }
        }
        small_world_barrier();
        if (lane == 0u) {
            smallWorldSortRecords[0].ordinal = atomicLoad(&telemetry[35]);
        }
        if (workgroupUniformLoad(
                &smallWorldSortRecords[0].ordinal) == 0u) {
            break;
        }
    }
    if (lane == 0u) { atomicStore(&telemetry[35], 0u); }

    for (var body = lane; body < params.capacities.x; body += 256u) {
        if (body_is_alive(body)) {
            let root = small_world_root(body);
            atomicStore(&bodyRoots[body], root);
            let record = KeyValue(body, root, body, body);
            var persistent = bodyPersistent[body];
            persistent.reserved0 = root;
            bodyPersistent[body] = persistent;
        }
    }
    small_world_barrier();

    // With no contacts, every live body is a singleton island and the only
    // sorting work is moving dead sentinels to the end. Give each lane four
    // consecutive body IDs, prefix their live counts, and scatter the exact
    // ascending stream without the fixed 55-barrier bitonic network.
    if (contactCount == 0u) {
        let firstBody = lane * 4u;
        var liveCount = 0u;
        for (var offset = 0u; offset < 4u; offset += 1u) {
            let body = firstBody + offset;
            liveCount += select(0u, 1u,
                body < params.capacities.x && body_is_alive(body));
        }
        smallWorldSortRecords[lane] = KeyValue(0u, 0u, 0u, liveCount);
        workgroupBarrier();
        if (lane == 0u) {
            var running = 0u;
            for (var worker = 0u; worker < 256u; worker += 1u) {
                let count = smallWorldSortRecords[worker].ordinal;
                smallWorldSortRecords[worker].ordinal = running;
                running += count;
            }
        }
        workgroupBarrier();
        var output = smallWorldSortRecords[lane].ordinal;
        for (var offset = 0u; offset < 4u; offset += 1u) {
            let body = firstBody + offset;
            if (body < params.capacities.x && body_is_alive(body)) {
                sortedBodyRecords[output] = KeyValue(body, atomicLoad(&bodyRoots[body]), body, body);
                output += 1u;
            }
        }
        small_world_barrier();
        for (var body = lane; body < params.capacities.x; body += 256u) {
            if (body_is_alive(body)) {
                let root = atomicLoad(&bodyRoots[body]);
                if (root == SENTINEL || atomicLoad(&bodyRoots[root]) != root) {
                    atomicAdd(&telemetry[11], 1u);
                }
            }
        }
        if (lane == 0u) {
            atomicStore(&telemetry[12], 0u);
        }
        return;
    }

    // The old rank scan performed O(n^2) comparisons. This fixed bitonic
    // network sorts the same unique (root, body) keys in O(n log^2 n).
    for (var index = lane; index < 1024u; index += 256u) {
        var record = sentinel_record();
        if (index < params.capacities.x) {
            if (body_is_alive(index)) {
                record = KeyValue(index, atomicLoad(&bodyRoots[index]), index, index);
            }
        }
        smallWorldSortRecords[index] = record;
    }
    workgroupBarrier();

    for (var width = 2u; width <= 1024u; width *= 2u) {
        for (var stride = width >> 1u; stride != 0u; stride >>= 1u) {
            for (var index = lane; index < 1024u; index += 256u) {
                let partner = index ^ stride;
                if (partner > index) {
                    let low = smallWorldSortRecords[index];
                    let high = smallWorldSortRecords[partner];
                    let ascending = (index & width) == 0u;
                    let swap = select(
                        body_record_less(low, high),
                        body_record_less(high, low), ascending);
                    if (swap) {
                        smallWorldSortRecords[index] = high;
                        smallWorldSortRecords[partner] = low;
                    }
                }
            }
            workgroupBarrier();
        }
    }

    for (var body = lane; body < params.capacities.x; body += 256u) {
        sortedBodyRecords[body] = smallWorldSortRecords[body];
        if (body_is_alive(body)) {
            let root = atomicLoad(&bodyRoots[body]);
            if (root == SENTINEL || atomicLoad(&bodyRoots[root]) != root) {
                atomicAdd(&telemetry[11], 1u);
            }
        }
    }
    if (lane == 0u) {
        atomicStore(&telemetry[12], unionRounds);
    }
}

@compute @workgroup_size(256)
fn small_world_decide(@builtin(global_invocation_id) gid : vec3<u32>) {
    let lane = gid.x;
    let firstTick = atomicLoad(&telemetry[13]) == 0u;
    let tick = atomicLoad(&telemetry[13]) + 1u;
    for (var body = lane; body < params.capacities.x; body += 256u) {
        islandRecords[body] = IslandRecord(SENTINEL, 0u, 0u, 0u);
        if (firstTick) {
            islandPersistent[body] = IslandPersistent(0u, 0u, 0u, 0u);
            bodyPersistent[body] = BodyPersistent(
                SENTINEL, 0u, bodyPersistent[body].reserved0, 0u);
        } else if (!body_is_alive(body)) {
            bodyPersistent[body] = BodyPersistent(SENTINEL, 0u, 0u, 0u);
        }
    }
    for (var index = lane; index < params.capacities.z; index += 256u) {
        islandEvents[index] = IslandEvent(SENTINEL, 0u, 0u, 0u);
    }
    small_world_barrier();

    for (var body = lane; body < params.capacities.x; body += 256u) {
        if (!body_is_alive(body)) { continue; }
        let slow = body_is_slow(body);
        var persistent = bodyPersistent[body];
        persistent.reserved1 = select(
            0u, min(persistent.reserved1 + 1u, params.control.y),
            slow && persistent.previousRoot != SENTINEL);
        bodyPersistent[body] = persistent;
    }
    small_world_barrier();

    if (lane == 0u) {
        smallWorldSortRecords[0].ordinal = select(
            0u, 1u, atomicLoad(&telemetry[12]) == 0u);
    }
    let contactFree = workgroupUniformLoad(
        &smallWorldSortRecords[0].ordinal) != 0u;
    if (contactFree) {
        // Without dynamic contacts every sorted body record is a singleton
        // island. Decide those independent islands in parallel while keeping
        // their canonical ascending record and event order.
        for (var islandIndex = lane; islandIndex < params.capacities.x;
             islandIndex += 256u) {
            let record = sortedBodyRecords[islandIndex];
            if (record.keyHigh == SENTINEL) { continue; }
            let root = record.keyHigh;
            let body = record.value;
            let awakeCount = select(0u, 1u,
                (u32(metadata[body].w) & BODY_AWAKE) != 0u);
            let quietTicks = bodyPersistent[body].reserved1;
            let wasSleeping = bodyPersistent[body].previousSleeping != 0u;
            let persistent = islandPersistent[root];
            let previousState = select(persistent.state, 1u,
                persistent.previousBodyCount == 0u && wasSleeping);
            var nextState = previousState;
            if (wasSleeping && awakeCount != 0u) {
                nextState = 0u;
            } else if (awakeCount == 0u) {
                nextState = 1u;
            } else {
                nextState = select(
                    0u, 1u, quietTicks >= params.control.y);
            }
            let eventType = select(0u,
                select(2u, 1u, nextState != 0u),
                nextState != previousState);
            islandPersistent[root] = IslandPersistent(
                quietTicks, nextState, 1u, eventType);
            islandRecords[islandIndex] = IslandRecord(
                root, islandIndex, 1u, nextState);
            atomicAdd(&telemetry[0], 1u);
            atomicMax(&telemetry[5], 1u);

            if (nextState != previousState) {
                atomicAdd(&telemetry[6], select(0u, 1u, nextState != 0u));
                atomicAdd(&telemetry[7], select(0u, 1u, nextState == 0u));
            }
            if (nextState != 0u) {
                atomicAdd(&telemetry[2], 1u);
                atomicAdd(&telemetry[4], 1u);
            } else {
                atomicAdd(&telemetry[1], 1u);
                atomicAdd(&telemetry[3], 1u);
            }
        }
    } else {
        if (lane == 0u) {
            var firstBodyRecord = 0u;
            var islandCount = 0u;
            while (firstBodyRecord < params.capacities.x) {
                let firstRecord = sortedBodyRecords[firstBodyRecord];
                if (firstRecord.keyHigh == SENTINEL) { break; }
                let root = firstRecord.keyHigh;
                var endBodyRecord = firstBodyRecord + 1u;
                while (endBodyRecord < params.capacities.x
                       && sortedBodyRecords[endBodyRecord].keyHigh == root) {
                    endBodyRecord += 1u;
                }
                smallWorldSortRecords[islandCount] = KeyValue(
                    root, firstBodyRecord,
                    endBodyRecord - firstBodyRecord, 0u);
                firstBodyRecord = endBodyRecord;
                islandCount += 1u;
            }
            smallWorldSortRecords[0].ordinal = islandCount;
        }
        let islandCount = workgroupUniformLoad(
            &smallWorldSortRecords[0].ordinal);
        for (var islandIndex = lane; islandIndex < islandCount;
             islandIndex += 256u) {
            let descriptor = smallWorldSortRecords[islandIndex];
            let root = descriptor.keyLow;
            let firstBodyRecord = descriptor.keyHigh;
            let bodyCount = descriptor.value;
            var awakeCount = 0u;
            var quietTicks = params.control.y;
            var wasSleeping = false;
            for (var offset = 0u; offset < bodyCount; offset += 1u) {
                let body = sortedBodyRecords[firstBodyRecord + offset].value;
                awakeCount += select(0u, 1u,
                    (u32(metadata[body].w) & BODY_AWAKE) != 0u);
                quietTicks = min(
                    quietTicks, bodyPersistent[body].reserved1);
                wasSleeping = wasSleeping
                    || bodyPersistent[body].previousSleeping != 0u;
            }
            let persistent = islandPersistent[root];
            let previousState = select(persistent.state, 1u,
                persistent.previousBodyCount == 0u && wasSleeping);
            var nextState = previousState;
            if (wasSleeping && awakeCount != 0u) {
                nextState = 0u;
            } else if (awakeCount == 0u) {
                nextState = 1u;
            } else {
                nextState = select(
                    0u, 1u, quietTicks >= params.control.y);
            }
            let eventType = select(0u,
                select(2u, 1u, nextState != 0u),
                nextState != previousState);
            islandPersistent[root] = IslandPersistent(
                quietTicks, nextState, bodyCount, eventType);
            islandRecords[islandIndex] = IslandRecord(
                root, firstBodyRecord, bodyCount, nextState);
            atomicAdd(&telemetry[0], 1u);
            atomicMax(&telemetry[5], bodyCount);

            if (nextState != previousState) {
                atomicAdd(&telemetry[6], select(0u, 1u, nextState != 0u));
                atomicAdd(&telemetry[7], select(0u, 1u, nextState == 0u));
            }
            if (nextState != 0u) {
                atomicAdd(&telemetry[2], 1u);
                atomicAdd(&telemetry[4], bodyCount);
            } else {
                atomicAdd(&telemetry[1], 1u);
                atomicAdd(&telemetry[3], bodyCount);
            }
        }
    }
    small_world_barrier();

    if (lane == 0u) {
        var eventCount = 0u;
        let islandCount = atomicLoad(&telemetry[0]);
        for (var islandIndex = 0u;
             islandIndex < islandCount; islandIndex += 1u) {
            let root = islandRecords[islandIndex].rootBody;
            let eventType = islandPersistent[root].reserved;
            if (eventType == 0u) { continue; }
            if (eventCount < params.capacities.z) {
                islandEvents[eventCount] = IslandEvent(
                    root, eventType, tick,
                    islandPersistent[root].previousBodyCount);
            }
            eventCount += 1u;
        }
        let outputEvents = min(eventCount, params.capacities.z);
        atomicStore(&telemetry[8], outputEvents);
        atomicStore(&telemetry[13], tick);
        atomicStore(&telemetry[17], select(
            0u, 1u, eventCount > params.capacities.z));
        atomicMax(&telemetry[14], atomicLoad(&telemetry[0]));
        atomicMax(&telemetry[15], atomicLoad(&telemetry[4]));
        atomicMax(&telemetry[19], outputEvents);
    }

    for (var body = lane; body < params.capacities.x; body += 256u) {
        if (!body_is_alive(body)) { continue; }
        let root = bodyPersistent[body].reserved0;
        let sleeping = islandPersistent[root].state != 0u;
        var bodyMetadata = metadata[body];
        var packedMetadata = u32(bodyMetadata.w);
        if (sleeping) {
            packedMetadata &= ~BODY_AWAKE;
            motions[body].linearVelocity_sleep = vec4<f32>(0.0);
            motions[body].angularVelocity_flags = vec4<f32>(0.0);
        } else {
            packedMetadata |= BODY_AWAKE;
        }
        bodyMetadata.w = bitcast<i32>(packedMetadata);
        metadata[body] = bodyMetadata;
        let persistent = bodyPersistent[body];
        let quietTicks = select(persistent.reserved1, 0u,
            persistent.previousSleeping != 0u && !sleeping);
        bodyPersistent[body] = BodyPersistent(
            root, select(0u, 1u, sleeping), 0u,
            select(quietTicks, params.control.y, sleeping));
    }
}

fn sleeping_record_less(a : KeyValue, b : KeyValue) -> bool {
    return a.keyHigh < b.keyHigh
        || (a.keyHigh == b.keyHigh && a.keyLow < b.keyLow)
        || (a.keyHigh == b.keyHigh && a.keyLow == b.keyLow
            && a.value < b.value);
}

@compute @workgroup_size(256)
fn small_world_sleeping_grid(@builtin(global_invocation_id) gid : vec3<u32>) {
    let lane = gid.x;
    for (var body = lane; body < params.capacities.x; body += 256u) {
        sleepingGrid[body] = sentinel_record();
        sortedSleepingGrid[body] = sentinel_record();
        sleepingCellRanges[body] = SleepingCellRange(
            SENTINEL, SENTINEL, 0u, 0u);
    }
    small_world_barrier();

    for (var body = lane; body < params.capacities.x; body += 256u) {
        if (!body_is_alive(body)
            || (u32(metadata[body].w) & BODY_AWAKE) != 0u) { continue; }
        var valid = false;
        let cell = sleeping_cell(body, &valid);
        if (!valid) {
            atomicStore(&telemetry[18], 1u);
            continue;
        }
        let key = encode_cell(cell);
        let record = KeyValue(key.x, key.y, body, body);
        sleepingGrid[body] = record;
        atomicAdd(&telemetry[9], 1u);
    }
    small_world_barrier();

    for (var body = lane; body < params.capacities.x; body += 256u) {
        let record = sleepingGrid[body];
        if (record.keyHigh == SENTINEL) { continue; }
        var rank = 0u;
        for (var other = 0u; other < params.capacities.x; other += 1u) {
            let otherRecord = sleepingGrid[other];
            if (otherRecord.keyHigh == SENTINEL) { continue; }
            rank += select(0u, 1u, sleeping_record_less(otherRecord, record));
        }
        sortedSleepingGrid[rank] = record;
    }
    small_world_barrier();

    if (lane == 0u) {
        let entryCount = atomicLoad(&telemetry[9]);
        var rangeCount = 0u;
        for (var index = 0u; index < entryCount; index += 1u) {
            let record = sortedSleepingGrid[index];
            var startsRange = index == 0u;
            if (!startsRange) {
                startsRange = record.keyLow
                        != sortedSleepingGrid[index - 1u].keyLow
                    || record.keyHigh
                        != sortedSleepingGrid[index - 1u].keyHigh;
            }
            if (startsRange) {
                sleepingCellRanges[rangeCount] = SleepingCellRange(
                    record.keyLow, record.keyHigh, index, 1u);
                rangeCount += 1u;
            } else {
                sleepingCellRanges[rangeCount - 1u].entryCount += 1u;
            }
        }
        atomicStore(&telemetry[10], rangeCount);
        atomicMax(&telemetry[16], entryCount);
    }
}
