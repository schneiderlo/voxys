const GENERATION_MASK : u32 = 0x000fffffu;
const BODY_ALIVE : u32 = 1u << 20u;
const BODY_AWAKE : u32 = 1u << 21u;
const SENTINEL : u32 = 0xffffffffu;
const CELL_MASK : u32 = 0x1fffffu;
const CELL_BIAS : i32 = 1048576;
const WORLD_SECTOR_SIZE : f32 = 256.0;

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

struct CellRange {
    keyLow : u32,
    keyHigh : u32,
    firstEntry : u32,
    entryCount : u32,
};

struct PersistentContact {
    pair : KeyValue,
    state : vec4<u32>,
};

struct ContactEvent {
    pairLow : u32,
    pairHigh : u32,
    eventType : u32,
    contactId : u32,
};

struct BroadPhaseParams {
    counts : vec4<u32>,
    capacities : vec4<u32>,
    grid : vec4<f32>,
};

@group(0) @binding(0) var<storage, read> poses : array<BodyPose>;
@group(0) @binding(1) var<storage, read> shapes : array<BodyShape>;
@group(0) @binding(2) var<storage, read> metadata : array<vec4<i32>>;
@group(0) @binding(3) var<storage, read_write> bodyEntryCounts : array<u32>;
@group(0) @binding(4) var<storage, read> bodyEntryOffsets : array<u32>;
@group(0) @binding(5) var<storage, read_write> gridEntries : array<KeyValue>;
@group(0) @binding(6) var<storage, read_write> telemetry : array<atomic<u32>>;
@group(0) @binding(7) var<uniform> broad : BroadPhaseParams;
@group(0) @binding(8) var<storage, read> sortedGridEntries : array<KeyValue>;
@group(0) @binding(9) var<storage, read_write> cellRanges : array<CellRange>;
@group(0) @binding(10) var<storage, read_write> oversizedFlags : array<u32>;
@group(0) @binding(11) var<storage, read_write> ownerPairCounts : array<u32>;
@group(0) @binding(12) var<storage, read> ownerPairOffsets : array<u32>;
@group(0) @binding(13) var<storage, read_write> pairCandidates : array<KeyValue>;
@group(0) @binding(14) var<storage, read> sortedPairCandidates : array<KeyValue>;
@group(0) @binding(15) var<storage, read_write> uniqueBodyPairs : array<KeyValue>;
@group(0) @binding(16) var<storage, read> previousContacts : array<PersistentContact>;
@group(0) @binding(17) var<storage, read_write> nextContacts : array<PersistentContact>;
@group(0) @binding(18) var<storage, read_write> contactOccupancy : array<u32>;
@group(0) @binding(19) var<storage, read_write> contactEvents : array<ContactEvent>;
@group(0) @binding(20) var<storage, read_write> lifecycleState : array<u32>;
@group(0) @binding(21) var<storage, read_write> rangePredicates : array<u32>;
@group(0) @binding(22) var<storage, read> entryRangeIndices : array<u32>;
@group(0) @binding(23) var<storage, read_write> sortDispatchArgs : array<u32>;
@group(0) @binding(24) var<storage, read_write> lifecycleNewPredicates : array<u32>;
@group(0) @binding(25) var<storage, read_write> lifecycleNewOffsets : array<u32>;
@group(0) @binding(26) var<storage, read_write> lifecycleEndPredicates : array<u32>;
@group(0) @binding(27) var<storage, read_write> lifecycleEndOffsets : array<u32>;
@group(0) @binding(28) var<storage, read_write> lifecycleFreePredicates : array<u32>;
@group(0) @binding(29) var<storage, read_write> lifecycleFreeOffsets : array<u32>;
@group(0) @binding(30) var<storage, read_write> lifecycleFreeIds : array<u32>;

fn sentinel_record() -> KeyValue {
    return KeyValue(SENTINEL, SENTINEL, SENTINEL, SENTINEL);
}

fn store_sort_dispatch(base : u32, count : u32) {
    sortDispatchArgs[base] = (count + broad.capacities.w - 1u)
        / broad.capacities.w;
    sortDispatchArgs[base + 1u] = 1u;
    sortDispatchArgs[base + 2u] = 1u;
    sortDispatchArgs[base + 3u] = select(0u, 1u, count != 0u);
    sortDispatchArgs[base + 4u] = 1u;
    sortDispatchArgs[base + 5u] = 1u;
}

fn key_less(lowA : u32, highA : u32, lowB : u32, highB : u32) -> bool {
    return highA < highB || (highA == highB && lowA < lowB);
}

fn occupied_range_count() -> u32 {
    return cellRanges[broad.counts.y].entryCount;
}

fn key_equal(a : KeyValue, b : KeyValue) -> bool {
    return a.keyLow == b.keyLow && a.keyHigh == b.keyHigh;
}

fn shape_radius(body : u32) -> f32 {
    return 0.5 * length(abs(shapes[body].dimensions_type.xyz)) + broad.grid.y;
}

fn body_flags(body : u32) -> u32 {
    return u32(metadata[body].w) & ~GENERATION_MASK;
}

fn adjacent_sector_delta(reference : i32, other : i32) -> i32 {
    if (other == reference) { return 0; }
    if (reference < 2147483647 && other == reference + 1) { return 1; }
    if (reference > -2147483647 - 1 && other == reference - 1) { return -1; }
    return 2;
}

fn position_delta(bodyA : u32, bodyB : u32,
                  valid : ptr<function, bool>) -> vec3<f32> {
    var sectorDelta = vec3<i32>(0);
    for (var axis = 0u; axis < 3u; axis += 1u) {
        sectorDelta[axis] = adjacent_sector_delta(
            metadata[bodyA][axis], metadata[bodyB][axis]);
        if (abs(sectorDelta[axis]) > 1) {
            (*valid) = false;
            return vec3<f32>(0.0);
        }
    }
    (*valid) = true;
    return poses[bodyB].position_invMass.xyz
         - poses[bodyA].position_invMass.xyz
         + vec3<f32>(sectorDelta) * WORLD_SECTOR_SIZE;
}

fn body_cell(body : u32, valid : ptr<function, bool>) -> vec3<i32> {
    let localCell = vec3<i32>(floor(
        poses[body].position_invMass.xyz / broad.grid.x));
    let sectors = metadata[body].xyz;
    if (all(sectors == vec3<i32>(0))) {
        (*valid) = coordinate_is_encodable(localCell);
        return localCell;
    }
    let cellsPerSector = i32(broad.grid.z);
    if (cellsPerSector <= 0) {
        (*valid) = false;
        return vec3<i32>(0);
    }
    // Keys are toroidal buckets, not authoritative world coordinates. The
    // lower 21 bits stay exact for every signed i32 sector without overflowing
    // signed arithmetic. position_delta() performs the exact sector test, so
    // distant sectors that alias to one bucket can add work but not contacts.
    let cellBits = bitcast<vec3<u32>>(sectors)
        * vec3<u32>(u32(cellsPerSector))
        + bitcast<vec3<u32>>(localCell)
        + vec3<u32>(u32(CELL_BIAS));
    let encoded = cellBits & vec3<u32>(CELL_MASK);
    (*valid) = true;
    return vec3<i32>(encoded) - vec3<i32>(CELL_BIAS);
}

fn body_is_alive(body : u32) -> bool {
    return body < broad.counts.x && (body_flags(body) & BODY_ALIVE) != 0u
        && max(abs(shapes[body].dimensions_type.x),
               max(abs(shapes[body].dimensions_type.y),
                   abs(shapes[body].dimensions_type.z))) > 0.0;
}

fn coordinate_is_encodable(value : vec3<i32>) -> bool {
    return all(value >= vec3<i32>(-CELL_BIAS))
        && all(value < vec3<i32>(CELL_BIAS));
}

fn encode_cell(value : vec3<i32>) -> vec2<u32> {
    let encoded = vec3<u32>(value + vec3<i32>(CELL_BIAS));
    let low = encoded.x | ((encoded.y & 0x7ffu) << 21u);
    let high = (encoded.y >> 11u) | (encoded.z << 10u);
    return vec2<u32>(low, high);
}

fn decode_cell(low : u32, high : u32) -> vec3<i32> {
    let x = low & CELL_MASK;
    let y = ((low >> 21u) & 0x7ffu) | ((high & 0x3ffu) << 11u);
    let z = (high >> 10u) & CELL_MASK;
    return vec3<i32>(vec3<u32>(x, y, z)) - vec3<i32>(CELL_BIAS);
}

fn wrap_cell(value : vec3<i32>) -> vec3<i32> {
    let biased = bitcast<vec3<u32>>(
        value + vec3<i32>(CELL_BIAS));
    let encoded = biased & vec3<u32>(CELL_MASK);
    return vec3<i32>(encoded) - vec3<i32>(CELL_BIAS);
}

struct CellBounds {
    minimum : vec3<i32>,
    maximum : vec3<i32>,
};

fn body_cell_bounds(body : u32) -> CellBounds {
    let radius = shape_radius(body);
    let position = poses[body].position_invMass.xyz;
    let minimum = vec3<i32>(floor((position - vec3<f32>(radius)) / broad.grid.x));
    let maximum = vec3<i32>(floor((position + vec3<f32>(radius)) / broad.grid.x));
    return CellBounds(minimum, maximum);
}

fn body_is_oversized(body : u32) -> bool {
    if (!body_is_alive(body)) { return false; }
    let radius = shape_radius(body);
    var valid = false;
    let cell = body_cell(body, &valid);
    return radius * 2.0 > broad.grid.x
        || !valid || !coordinate_is_encodable(cell);
}

@compute @workgroup_size(1)
fn reset_telemetry(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    for (var index = 0u; index < 14u; index += 1u) {
        atomicStore(&telemetry[index], 0u);
    }
}

fn clear_grid_entries_impl(gid : vec3<u32>) {
    if (gid.x < broad.counts.y) { gridEntries[gid.x] = sentinel_record(); }
}

@compute @workgroup_size(64)
fn clear_grid_entries_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    clear_grid_entries_impl(gid);
}
@compute @workgroup_size(128)
fn clear_grid_entries_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    clear_grid_entries_impl(gid);
}
@compute @workgroup_size(256)
fn clear_grid_entries_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    clear_grid_entries_impl(gid);
}

fn count_grid_entries_impl(gid : vec3<u32>) {
    let body = gid.x;
    if (body >= broad.counts.x) { return; }
    bodyEntryCounts[body] = 0u;
    oversizedFlags[body] = 0u;
    if (!body_is_alive(body)) { return; }
    // Center-cell insertion plus the 13-cell forward neighborhood is exact
    // while any two common-body radii sum to at most one cell width.
    if (body_is_oversized(body)) {
        oversizedFlags[body] = 1u;
        atomicAdd(&telemetry[5], 1u);
        return;
    }
    bodyEntryCounts[body] = 1u;
}

@compute @workgroup_size(64)
fn count_grid_entries_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    count_grid_entries_impl(gid);
}
@compute @workgroup_size(128)
fn count_grid_entries_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    count_grid_entries_impl(gid);
}
@compute @workgroup_size(256)
fn count_grid_entries_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    count_grid_entries_impl(gid);
}

fn scatter_grid_entries_impl(gid : vec3<u32>) {
    let body = gid.x;
    if (body >= broad.counts.x || bodyEntryCounts[body] == 0u) { return; }
    var valid = false;
    let cell = body_cell(body, &valid);
    if (!valid) { return; }
    let key = encode_cell(cell);
    let output = bodyEntryOffsets[body];
    if (output < broad.counts.y) {
        gridEntries[output] = KeyValue(
            key.x, key.y, body,
            select(1u, 0u, (body_flags(body) & BODY_AWAKE) != 0u));
    }
}

@compute @workgroup_size(64)
fn scatter_grid_entries_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_grid_entries_impl(gid);
}
@compute @workgroup_size(128)
fn scatter_grid_entries_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_grid_entries_impl(gid);
}
@compute @workgroup_size(256)
fn scatter_grid_entries_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_grid_entries_impl(gid);
}

@compute @workgroup_size(1)
fn finalize_grid_entry_count(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    var entryCount = 0u;
    if (broad.counts.x != 0u) {
        let last = broad.counts.x - 1u;
        entryCount = min(bodyEntryOffsets[last] + bodyEntryCounts[last],
                         broad.counts.y);
    }
    atomicStore(&telemetry[0], entryCount);
    atomicMax(&telemetry[14], entryCount);
    store_sort_dispatch(0u, entryCount);
}

fn mark_cell_range_starts_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= broad.counts.y) { return; }
    var start = 0u;
    if (index < atomicLoad(&telemetry[0])) {
        if (index == 0u) {
            start = 1u;
        } else {
            let current = sortedGridEntries[index];
            let previous = sortedGridEntries[index - 1u];
            start = select(0u, 1u, current.keyLow != previous.keyLow
                                     || current.keyHigh != previous.keyHigh);
        }
    }
    rangePredicates[index] = start;
}

@compute @workgroup_size(64)
fn mark_cell_range_starts_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_cell_range_starts_impl(gid);
}
@compute @workgroup_size(128)
fn mark_cell_range_starts_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_cell_range_starts_impl(gid);
}
@compute @workgroup_size(256)
fn mark_cell_range_starts_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    mark_cell_range_starts_impl(gid);
}

@compute @workgroup_size(1)
fn finalize_cell_range_count(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    let entryCount = atomicLoad(&telemetry[0]);
    var rangeCount = 0u;
    if (entryCount != 0u) {
        let last = entryCount - 1u;
        rangeCount = entryRangeIndices[last] + rangePredicates[last];
    }
    atomicStore(&telemetry[1], rangeCount);
    cellRanges[broad.counts.y].entryCount = rangeCount;
    oversizedFlags[broad.counts.x] = rangeCount;
    oversizedFlags[broad.counts.x + 1u] = rangeCount + broad.counts.x;
    atomicMax(&telemetry[15], rangeCount);
    store_sort_dispatch(6u, rangeCount + broad.counts.x);
}

fn scatter_cell_range_starts_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= atomicLoad(&telemetry[0])
        || rangePredicates[index] == 0u) { return; }
    let record = sortedGridEntries[index];
    let range = entryRangeIndices[index];
    cellRanges[range] = CellRange(
        record.keyLow, record.keyHigh, index, 0u);
}

@compute @workgroup_size(64)
fn scatter_cell_range_starts_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_cell_range_starts_impl(gid);
}
@compute @workgroup_size(128)
fn scatter_cell_range_starts_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_cell_range_starts_impl(gid);
}
@compute @workgroup_size(256)
fn scatter_cell_range_starts_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_cell_range_starts_impl(gid);
}

fn scatter_cell_range_ends_impl(gid : vec3<u32>) {
    let index = gid.x;
    let entryCount = atomicLoad(&telemetry[0]);
    if (index >= entryCount) { return; }
    var isEnd = index + 1u == entryCount;
    if (!isEnd) {
        let current = sortedGridEntries[index];
        let next = sortedGridEntries[index + 1u];
        isEnd = current.keyLow != next.keyLow
             || current.keyHigh != next.keyHigh;
    }
    if (!isEnd) { return; }
    var range = entryRangeIndices[index];
    if (rangePredicates[index] == 0u) { range -= 1u; }
    cellRanges[range].entryCount = index + 1u
        - cellRanges[range].firstEntry;
}

@compute @workgroup_size(64)
fn scatter_cell_range_ends_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_cell_range_ends_impl(gid);
}
@compute @workgroup_size(128)
fn scatter_cell_range_ends_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_cell_range_ends_impl(gid);
}
@compute @workgroup_size(256)
fn scatter_cell_range_ends_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_cell_range_ends_impl(gid);
}

fn find_cell_range(key : vec2<u32>) -> u32 {
    var low = 0u;
    var high = occupied_range_count();
    while (low < high) {
        let middle = low + (high - low) / 2u;
        let candidate = cellRanges[middle];
        if (key_less(candidate.keyLow, candidate.keyHigh, key.x, key.y)) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    if (low < occupied_range_count()
        && cellRanges[low].keyLow == key.x
        && cellRanges[low].keyHigh == key.y) {
        return low;
    }
    return SENTINEL;
}

fn bodies_overlap(bodyA : u32, bodyB : u32) -> bool {
    if (bodyA == bodyB || !body_is_alive(bodyA) || !body_is_alive(bodyB)) {
        return false;
    }
    if ((body_flags(bodyA) & BODY_AWAKE) == 0u
        && (body_flags(bodyB) & BODY_AWAKE) == 0u) { return false; }
    let extent = shape_radius(bodyA) + shape_radius(bodyB);
    var valid = false;
    let delta = position_delta(bodyA, bodyB, &valid);
    return valid && all(abs(delta) <= vec3<f32>(extent));
}

fn count_range_pairs(rangeA : CellRange, rangeB : CellRange,
                     sameRange : bool) -> u32 {
    var count = 0u;
    for (var localA = 0u; localA < rangeA.entryCount; localA += 1u) {
        let bodyA = sortedGridEntries[rangeA.firstEntry + localA].value;
        let startB = select(0u, localA + 1u, sameRange);
        for (var localB = startB; localB < rangeB.entryCount; localB += 1u) {
            let bodyB = sortedGridEntries[rangeB.firstEntry + localB].value;
            count += select(0u, 1u, bodies_overlap(bodyA, bodyB));
        }
    }
    return count;
}

fn count_pairs_for_owner(owner : u32) -> u32 {
    let rangeCount = occupied_range_count();
    if (owner < rangeCount) {
        let range = cellRanges[owner];
        if (range.keyHigh == SENTINEL) { return 0u; }
        var count = count_range_pairs(range, range, true);
        let cell = decode_cell(range.keyLow, range.keyHigh);
        for (var dx = -1; dx <= 1; dx += 1) {
            for (var dy = -1; dy <= 1; dy += 1) {
                for (var dz = -1; dz <= 1; dz += 1) {
                    let forward = dx > 0
                        || (dx == 0 && dy > 0)
                        || (dx == 0 && dy == 0 && dz > 0);
                    if (!forward) { continue; }
                    let neighborCell = wrap_cell(
                        cell + vec3<i32>(dx, dy, dz));
                    let neighbor = find_cell_range(encode_cell(neighborCell));
                    if (neighbor != SENTINEL) {
                        count += count_range_pairs(
                            range, cellRanges[neighbor], false);
                    }
                }
            }
        }
        return count;
    }
    let body = owner - rangeCount;
    if (body >= broad.counts.x || !body_is_oversized(body)) { return 0u; }
    var count = 0u;
    for (var other = 0u; other < broad.counts.x; other += 1u) {
        if (other == body) { continue; }
        if (body_is_oversized(other) && other < body) { continue; }
        count += select(0u, 1u, bodies_overlap(body, other));
    }
    return count;
}

fn count_pairs_impl(gid : vec3<u32>) {
    if (gid.x < broad.counts.z) {
        ownerPairCounts[gid.x] = count_pairs_for_owner(gid.x);
    }
}

@compute @workgroup_size(64)
fn count_pairs_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    count_pairs_impl(gid);
}
@compute @workgroup_size(128)
fn count_pairs_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    count_pairs_impl(gid);
}
@compute @workgroup_size(256)
fn count_pairs_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    count_pairs_impl(gid);
}

fn emit_pair(bodyA : u32, bodyB : u32, output : u32) {
    if (output >= broad.counts.w) { return; }
    let minimum = min(bodyA, bodyB);
    let maximum = max(bodyA, bodyB);
    let sleeping = select(0u, 1u,
        (body_flags(minimum) & BODY_AWAKE) == 0u
        || (body_flags(maximum) & BODY_AWAKE) == 0u);
    pairCandidates[output] = KeyValue(maximum, minimum, sleeping, output);
}

fn scatter_range_pairs(rangeA : CellRange, rangeB : CellRange,
                       sameRange : bool, base : u32,
                       localOffset : ptr<function, u32>) {
    for (var localA = 0u; localA < rangeA.entryCount; localA += 1u) {
        let bodyA = sortedGridEntries[rangeA.firstEntry + localA].value;
        let startB = select(0u, localA + 1u, sameRange);
        for (var localB = startB; localB < rangeB.entryCount; localB += 1u) {
            let bodyB = sortedGridEntries[rangeB.firstEntry + localB].value;
            if (bodies_overlap(bodyA, bodyB)) {
                emit_pair(bodyA, bodyB, base + (*localOffset));
                (*localOffset) += 1u;
            }
        }
    }
}

fn scatter_pairs_impl(gid : vec3<u32>) {
    let owner = gid.x;
    if (owner >= broad.counts.z || ownerPairCounts[owner] == 0u) { return; }
    let base = ownerPairOffsets[owner];
    var local = 0u;
    let rangeCount = occupied_range_count();
    if (owner < rangeCount) {
        let range = cellRanges[owner];
        if (range.keyHigh == SENTINEL) { return; }
        scatter_range_pairs(range, range, true, base, &local);
        let cell = decode_cell(range.keyLow, range.keyHigh);
        for (var dx = -1; dx <= 1; dx += 1) {
            for (var dy = -1; dy <= 1; dy += 1) {
                for (var dz = -1; dz <= 1; dz += 1) {
                    let forward = dx > 0
                        || (dx == 0 && dy > 0)
                        || (dx == 0 && dy == 0 && dz > 0);
                    if (!forward) { continue; }
                    let neighborCell = wrap_cell(
                        cell + vec3<i32>(dx, dy, dz));
                    let neighbor = find_cell_range(encode_cell(neighborCell));
                    if (neighbor != SENTINEL) {
                        scatter_range_pairs(
                            range, cellRanges[neighbor], false, base, &local);
                    }
                }
            }
        }
        return;
    }
    let body = owner - rangeCount;
    if (body >= broad.counts.x || !body_is_oversized(body)) { return; }
    for (var other = 0u; other < broad.counts.x; other += 1u) {
        if (other == body) { continue; }
        if (body_is_oversized(other) && other < body) { continue; }
        if (bodies_overlap(body, other)) {
            emit_pair(body, other, base + local);
            local += 1u;
        }
    }
}

fn clear_pair_candidates_impl(gid : vec3<u32>) {
    if (gid.x == 0u) {
        var rawCandidates = 0u;
        let ownerCount = oversizedFlags[broad.counts.x + 1u];
        if (ownerCount != 0u) {
            let last = ownerCount - 1u;
            rawCandidates = ownerPairOffsets[last] + ownerPairCounts[last];
        }
        atomicStore(&telemetry[2], rawCandidates);
        store_sort_dispatch(12u, min(rawCandidates, broad.counts.w));
    }
}

@compute @workgroup_size(64)
fn clear_pair_candidates_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    clear_pair_candidates_impl(gid);
}
@compute @workgroup_size(128)
fn clear_pair_candidates_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    clear_pair_candidates_impl(gid);
}
@compute @workgroup_size(256)
fn clear_pair_candidates_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    clear_pair_candidates_impl(gid);
}

@compute @workgroup_size(64)
fn scatter_pairs_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_pairs_impl(gid);
}
@compute @workgroup_size(128)
fn scatter_pairs_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_pairs_impl(gid);
}
@compute @workgroup_size(256)
fn scatter_pairs_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    scatter_pairs_impl(gid);
}

fn unique_pairs_impl(gid : vec3<u32>) {
    let rawCandidates = atomicLoad(&telemetry[2]);
    let materialized = min(rawCandidates, broad.counts.w);
    // Each common body owns exactly one center-cell record, neighbor checks
    // are forward-only, and oversized/oversized pairs have an ID tie-break.
    // Pair generation is therefore unique by construction. Keep the radix
    // sort for canonical ordering, then copy it in parallel.
    let index = gid.x;
    if (index < min(materialized, broad.capacities.x)) {
        let record = sortedPairCandidates[index];
        uniqueBodyPairs[index] = KeyValue(
            record.keyLow, record.keyHigh, record.value, index);
        if (record.value != 0u) {
            atomicAdd(&telemetry[4], 1u);
        }
    }
    if (index == 0u) {
        atomicStore(&telemetry[3], materialized);
        atomicStore(&telemetry[9],
            select(0u, 1u, rawCandidates > broad.counts.w));
        atomicStore(&telemetry[10],
            select(0u, 1u, materialized > broad.capacities.x));
        atomicMax(&telemetry[16], rawCandidates);
        atomicMax(&telemetry[17], materialized);
    }
}

@compute @workgroup_size(64)
fn unique_pairs_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    unique_pairs_impl(gid);
}
@compute @workgroup_size(128)
fn unique_pairs_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    unique_pairs_impl(gid);
}
@compute @workgroup_size(256)
fn unique_pairs_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    unique_pairs_impl(gid);
}

fn pair_equal_contact(pair : KeyValue, contact : PersistentContact) -> bool {
    return pair.keyLow == contact.pair.keyLow
        && pair.keyHigh == contact.pair.keyHigh;
}

fn lifecycle_current_count() -> u32 {
    return min(atomicLoad(&telemetry[3]),
               min(broad.capacities.x, broad.capacities.y));
}

fn lifecycle_previous_count() -> u32 {
    return min(lifecycleState[0], broad.capacities.y);
}

fn find_previous_contact(pair : KeyValue) -> u32 {
    var low = 0u;
    var high = lifecycle_previous_count();
    while (low < high) {
        let middle = low + (high - low) / 2u;
        let candidate = previousContacts[middle].pair;
        if (key_less(candidate.keyLow, candidate.keyHigh,
                     pair.keyLow, pair.keyHigh)) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    if (low < lifecycle_previous_count()
        && pair_equal_contact(pair, previousContacts[low])) {
        return low;
    }
    return SENTINEL;
}

fn find_current_pair(contact : PersistentContact) -> u32 {
    var low = 0u;
    var high = lifecycle_current_count();
    while (low < high) {
        let middle = low + (high - low) / 2u;
        let candidate = uniqueBodyPairs[middle];
        if (key_less(candidate.keyLow, candidate.keyHigh,
                     contact.pair.keyLow, contact.pair.keyHigh)) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    if (low < lifecycle_current_count()
        && pair_equal_contact(uniqueBodyPairs[low], contact)) {
        return low;
    }
    return SENTINEL;
}

fn lifecycle_reset_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= broad.capacities.y) { return; }
    nextContacts[index].pair = sentinel_record();
    nextContacts[index].state = vec4<u32>(0u);
    contactOccupancy[index] = 0u;
    lifecycleNewPredicates[index] = 0u;
    lifecycleEndPredicates[index] = 0u;
}

fn lifecycle_prepare_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index == 0u) {
        lifecycleState[2] = lifecycle_current_count();
    }
    if (index < lifecycle_current_count()) {
        let current = uniqueBodyPairs[index];
        let previousIndex = find_previous_contact(current);
        nextContacts[index].pair = current;
        if (previousIndex != SENTINEL) {
            var state = previousContacts[previousIndex].state;
            state.y += 1u;
            state.w = 0u;
            nextContacts[index].state = state;
            if (state.x < broad.capacities.y) {
                contactOccupancy[state.x] = 1u;
            } else {
                nextContacts[index].state.x = SENTINEL;
                lifecycleNewPredicates[index] = 1u;
            }
        } else {
            nextContacts[index].state = vec4<u32>(
                SENTINEL, 0u, SENTINEL, 0u);
            lifecycleNewPredicates[index] = 1u;
        }
    }
    if (index < lifecycle_previous_count()
        && find_current_pair(previousContacts[index]) == SENTINEL) {
        lifecycleEndPredicates[index] = 1u;
    }
}

fn lifecycle_mark_free_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index < broad.capacities.y) {
        lifecycleFreePredicates[index] = select(
            0u, 1u, contactOccupancy[index] == 0u);
    }
}

fn lifecycle_scatter_free_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index == 0u) {
        lifecycleState[3] = lifecycle_free_count();
    }
    if (index < broad.capacities.y
        && lifecycleFreePredicates[index] != 0u) {
        lifecycleFreeIds[lifecycleFreeOffsets[index]] = index;
    }
}

fn lifecycle_new_count() -> u32 {
    if (broad.capacities.y == 0u) { return 0u; }
    let last = broad.capacities.y - 1u;
    return lifecycleNewOffsets[last] + lifecycleNewPredicates[last];
}

fn lifecycle_end_count() -> u32 {
    if (broad.capacities.y == 0u) { return 0u; }
    let last = broad.capacities.y - 1u;
    return lifecycleEndOffsets[last] + lifecycleEndPredicates[last];
}

fn lifecycle_free_count() -> u32 {
    if (broad.capacities.y == 0u) { return 0u; }
    let last = broad.capacities.y - 1u;
    return lifecycleFreeOffsets[last] + lifecycleFreePredicates[last];
}

fn lifecycle_assign_begin_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= lifecycleState[2]
        || lifecycleNewPredicates[index] == 0u) { return; }
    let rank = lifecycleNewOffsets[index];
    let freeCount = lifecycleState[3];
    if (rank >= freeCount) { return; }
    let contactId = lifecycleFreeIds[rank];
    nextContacts[index].state = vec4<u32>(
        contactId, 0u, SENTINEL, 0u);
    contactOccupancy[contactId] = 1u;
    let pair = nextContacts[index].pair;
    contactEvents[rank] = ContactEvent(
        pair.keyLow, pair.keyHigh, 1u, contactId);
}

fn lifecycle_scatter_end_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= lifecycle_previous_count()
        || lifecycleEndPredicates[index] == 0u) { return; }
    let rank = lifecycleEndOffsets[index];
    let previous = previousContacts[index];
    contactEvents[broad.capacities.y + rank] = ContactEvent(
        previous.pair.keyLow, previous.pair.keyHigh, 2u,
        previous.state.x);
}

@compute @workgroup_size(64)
fn lifecycle_reset_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_reset_impl(gid);
}
@compute @workgroup_size(128)
fn lifecycle_reset_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_reset_impl(gid);
}
@compute @workgroup_size(256)
fn lifecycle_reset_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_reset_impl(gid);
}

@compute @workgroup_size(64)
fn lifecycle_prepare_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_prepare_impl(gid);
}
@compute @workgroup_size(128)
fn lifecycle_prepare_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_prepare_impl(gid);
}
@compute @workgroup_size(256)
fn lifecycle_prepare_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_prepare_impl(gid);
}

@compute @workgroup_size(64)
fn lifecycle_mark_free_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_mark_free_impl(gid);
}
@compute @workgroup_size(128)
fn lifecycle_mark_free_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_mark_free_impl(gid);
}
@compute @workgroup_size(256)
fn lifecycle_mark_free_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_mark_free_impl(gid);
}

@compute @workgroup_size(64)
fn lifecycle_scatter_free_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_scatter_free_impl(gid);
}
@compute @workgroup_size(128)
fn lifecycle_scatter_free_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_scatter_free_impl(gid);
}
@compute @workgroup_size(256)
fn lifecycle_scatter_free_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_scatter_free_impl(gid);
}

@compute @workgroup_size(64)
fn lifecycle_assign_begin_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_assign_begin_impl(gid);
}
@compute @workgroup_size(128)
fn lifecycle_assign_begin_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_assign_begin_impl(gid);
}
@compute @workgroup_size(256)
fn lifecycle_assign_begin_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_assign_begin_impl(gid);
}

@compute @workgroup_size(64)
fn lifecycle_scatter_end_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_scatter_end_impl(gid);
}
@compute @workgroup_size(128)
fn lifecycle_scatter_end_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_scatter_end_impl(gid);
}
@compute @workgroup_size(256)
fn lifecycle_scatter_end_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    lifecycle_scatter_end_impl(gid);
}

@compute @workgroup_size(1)
fn lifecycle_finalize(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    let outputCount = lifecycle_current_count();
    let beginCount = lifecycle_new_count();
    let endCount = lifecycle_end_count();
    let freeCount = lifecycle_free_count();
    lifecycleState[0] = outputCount;
    lifecycleState[1] += 1u;
    atomicStore(&telemetry[6], outputCount);
    atomicStore(&telemetry[7], beginCount);
    atomicStore(&telemetry[8], endCount);
    atomicOr(&telemetry[11], select(
        0u, 1u, atomicLoad(&telemetry[3]) > broad.capacities.y
            || beginCount > freeCount));
    atomicStore(&telemetry[12], select(0u, 1u,
        beginCount > broad.capacities.y || endCount > broad.capacities.y));
    atomicMax(&telemetry[18], outputCount);
    atomicStore(&telemetry[19], lifecycleState[1]);
    atomicMax(&telemetry[20], beginCount + endCount);
}
