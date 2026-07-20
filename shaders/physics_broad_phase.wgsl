const GENERATION_MASK : u32 = 0x000fffffu;
const BODY_ALIVE : u32 = 1u << 20u;
const BODY_AWAKE : u32 = 1u << 21u;
const SENTINEL : u32 = 0xffffffffu;
const CELL_MASK : u32 = 0x1fffffu;
const CELL_BIAS : i32 = 1048576;
const WORLD_SECTOR_SIZE : f32 = 256.0;
const SMALL_LIFECYCLE_CONTACT_LIMIT : u32 = 1024u;
const SPATIAL_CLASS_COMMON : u32 = 0u;
const SPATIAL_CLASS_PHYSICAL_OVERSIZED : u32 = 1u;
const SPATIAL_CLASS_SWEPT_OVERSIZED : u32 = 2u;
// Must match physics_ballistic.wgsl. The fractional shape-type payload is a
// conservative one-tick linear travel bound for tunnelling-risk bodies.
const SHAPE_SWEEP_RANGE : f32 = 256.0;

struct BodyPose {
    position_invMass : vec4<f32>,
    orientation : vec4<f32>,
};

struct BodyShape {
    dimensions_type : vec4<f32>,
    invInertia_material : vec4<f32>,
    material_coefficients : vec4<f32>,
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
    identity : vec4<u32>,
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
@group(0) @binding(31) var<storage, read_write> mediumPairPredicates : array<u32>;
@group(0) @binding(32) var<storage, read_write> mediumPairPredicatesTail : array<u32>;

var<workgroup> smallPairFlags : array<u32, 256>;
var<workgroup> smallPairOffsets : array<u32, 256>;
var<workgroup> smallPairPositionRadius : array<vec4<f32>, 256>;
var<workgroup> smallPairSectorFlags : array<vec4<i32>, 256>;
var<workgroup> smallPairCellKeys : array<vec2<u32>, 256>;
var<workgroup> smallPairOutputBase : u32;
var<workgroup> smallPairUseCellCull : u32;

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

fn body_flags(body : u32) -> u32 {
    return u32(metadata[body].w) & ~GENERATION_MASK;
}

fn shape_sweep_distance(body : u32) -> f32 {
    if ((body_flags(body) & BODY_AWAKE) == 0u) { return 0.0; }
    return fract(max(shapes[body].dimensions_type.w, 0.0))
        * SHAPE_SWEEP_RANGE;
}

fn physical_shape_radius(body : u32) -> f32 {
    return 0.5 * length(abs(shapes[body].dimensions_type.xyz)) + broad.grid.y;
}

fn shape_radius(body : u32) -> f32 {
    return physical_shape_radius(body) + shape_sweep_distance(body);
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

fn swept_search_cell_bounds(body : u32) -> CellBounds {
    // The larger-sweep body owns a swept/swept pair. Its own sweep therefore
    // bounds the other sweep, while every center-cell body's physical radius
    // is at most half a cell.
    let radius = physical_shape_radius(body)
        + 2.0 * shape_sweep_distance(body) + 0.5 * broad.grid.x;
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

fn body_spatial_class(body : u32) -> u32 {
    if (!body_is_alive(body)) { return SPATIAL_CLASS_COMMON; }
    var valid = false;
    let cell = body_cell(body, &valid);
    if (physical_shape_radius(body) * 2.0 > broad.grid.x
        || !valid || !coordinate_is_encodable(cell)) {
        return SPATIAL_CLASS_PHYSICAL_OVERSIZED;
    }
    if (shape_radius(body) * 2.0 > broad.grid.x) {
        return SPATIAL_CLASS_SWEPT_OVERSIZED;
    }
    return SPATIAL_CLASS_COMMON;
}

fn swept_pair_is_owned(body : u32, other : u32) -> bool {
    if (body_spatial_class(other) != SPATIAL_CLASS_SWEPT_OVERSIZED) {
        return true;
    }
    let bodySweep = shape_sweep_distance(body);
    let otherSweep = shape_sweep_distance(other);
    return bodySweep > otherSweep
        || (bodySweep == otherSweep && body < other);
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
    let spatialClass = body_spatial_class(body);
    oversizedFlags[body] = spatialClass;
    if (spatialClass != SPATIAL_CLASS_COMMON) {
        atomicAdd(&telemetry[5], 1u);
    }
    if (spatialClass == SPATIAL_CLASS_PHYSICAL_OVERSIZED) {
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

fn candidate_count_limit() -> u32 {
    // One value beyond capacity is enough to preserve the overflow signal.
    // Saturating here also prevents a pathological dense scene from wrapping
    // the u32 owner prefix back into the writable candidate range.
    return select(broad.counts.w + 1u, broad.counts.w,
                  broad.counts.w == SENTINEL);
}

fn count_range_pairs(rangeA : CellRange, rangeB : CellRange,
                     sameRange : bool, limit : u32) -> u32 {
    var count = 0u;
    for (var localA = 0u; localA < rangeA.entryCount; localA += 1u) {
        let bodyA = sortedGridEntries[rangeA.firstEntry + localA].value;
        let startB = select(0u, localA + 1u, sameRange);
        for (var localB = startB; localB < rangeB.entryCount; localB += 1u) {
            let bodyB = sortedGridEntries[rangeB.firstEntry + localB].value;
            if (bodies_overlap(bodyA, bodyB)) {
                count += 1u;
                if (count >= limit) { return limit; }
            }
        }
    }
    return count;
}

fn count_swept_body_pairs(body : u32, limit : u32) -> u32 {
    var centerValid = false;
    let centerCell = body_cell(body, &centerValid);
    if (!centerValid) { return 0u; }
    let localCenter = vec3<i32>(floor(
        poses[body].position_invMass.xyz / broad.grid.x));
    let bounds = swept_search_cell_bounds(body);
    let minimumOffset = bounds.minimum - localCenter;
    let maximumOffset = bounds.maximum - localCenter;
    var count = 0u;
    for (var dx = minimumOffset.x; dx <= maximumOffset.x; dx += 1) {
        for (var dy = minimumOffset.y; dy <= maximumOffset.y; dy += 1) {
            for (var dz = minimumOffset.z; dz <= maximumOffset.z; dz += 1) {
                // The ordinary center-cell path already owns this neighborhood.
                if (abs(dx) <= 1 && abs(dy) <= 1 && abs(dz) <= 1) {
                    continue;
                }
                let cell = wrap_cell(centerCell + vec3<i32>(dx, dy, dz));
                let rangeIndex = find_cell_range(encode_cell(cell));
                if (rangeIndex == SENTINEL) { continue; }
                let range = cellRanges[rangeIndex];
                for (var local = 0u; local < range.entryCount; local += 1u) {
                    let other = sortedGridEntries[range.firstEntry + local].value;
                    if (other == body) { continue; }
                    if (!swept_pair_is_owned(body, other)) { continue; }
                    if (bodies_overlap(body, other)) {
                        count += 1u;
                        if (count >= limit) { return limit; }
                    }
                }
            }
        }
    }
    return count;
}

fn count_pairs_for_owner(owner : u32) -> u32 {
    let rangeCount = occupied_range_count();
    let limit = candidate_count_limit();
    if (owner < rangeCount) {
        let range = cellRanges[owner];
        if (range.keyHigh == SENTINEL) { return 0u; }
        var count = count_range_pairs(range, range, true, limit);
        if (count >= limit) { return limit; }
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
                        count += count_range_pairs(range, cellRanges[neighbor],
                                                   false, limit - count);
                        if (count >= limit) { return limit; }
                    }
                }
            }
        }
        return count;
    }
    let body = owner - rangeCount;
    if (body >= broad.counts.x) { return 0u; }
    let spatialClass = body_spatial_class(body);
    if (spatialClass == SPATIAL_CLASS_COMMON) { return 0u; }
    if (spatialClass == SPATIAL_CLASS_SWEPT_OVERSIZED) {
        return count_swept_body_pairs(body, limit);
    }
    var count = 0u;
    for (var other = 0u; other < broad.counts.x; other += 1u) {
        if (other == body) { continue; }
        if (body_spatial_class(other) == SPATIAL_CLASS_PHYSICAL_OVERSIZED
            && other < body) { continue; }
        if (bodies_overlap(body, other)) {
            count += 1u;
            if (count >= limit) { return limit; }
        }
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
                       localOffset : ptr<function, u32>) -> bool {
    if (base >= broad.counts.w) { return true; }
    for (var localA = 0u; localA < rangeA.entryCount; localA += 1u) {
        let bodyA = sortedGridEntries[rangeA.firstEntry + localA].value;
        let startB = select(0u, localA + 1u, sameRange);
        for (var localB = startB; localB < rangeB.entryCount; localB += 1u) {
            let bodyB = sortedGridEntries[rangeB.firstEntry + localB].value;
            if (bodies_overlap(bodyA, bodyB)) {
                if ((*localOffset) >= broad.counts.w - base) { return true; }
                emit_pair(bodyA, bodyB, base + (*localOffset));
                (*localOffset) += 1u;
            }
        }
    }
    return false;
}

fn scatter_swept_body_pairs(body : u32, base : u32,
                            localOffset : ptr<function, u32>) {
    var centerValid = false;
    let centerCell = body_cell(body, &centerValid);
    if (!centerValid || base >= broad.counts.w) { return; }
    let localCenter = vec3<i32>(floor(
        poses[body].position_invMass.xyz / broad.grid.x));
    let bounds = swept_search_cell_bounds(body);
    let minimumOffset = bounds.minimum - localCenter;
    let maximumOffset = bounds.maximum - localCenter;
    for (var dx = minimumOffset.x; dx <= maximumOffset.x; dx += 1) {
        for (var dy = minimumOffset.y; dy <= maximumOffset.y; dy += 1) {
            for (var dz = minimumOffset.z; dz <= maximumOffset.z; dz += 1) {
                if (abs(dx) <= 1 && abs(dy) <= 1 && abs(dz) <= 1) {
                    continue;
                }
                let cell = wrap_cell(centerCell + vec3<i32>(dx, dy, dz));
                let rangeIndex = find_cell_range(encode_cell(cell));
                if (rangeIndex == SENTINEL) { continue; }
                let range = cellRanges[rangeIndex];
                for (var local = 0u; local < range.entryCount; local += 1u) {
                    if ((*localOffset) >= broad.counts.w - base) { return; }
                    let other = sortedGridEntries[range.firstEntry + local].value;
                    if (other == body) { continue; }
                    if (!swept_pair_is_owned(body, other)) { continue; }
                    if (bodies_overlap(body, other)) {
                        emit_pair(body, other, base + (*localOffset));
                        (*localOffset) += 1u;
                    }
                }
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
        if (scatter_range_pairs(range, range, true, base, &local)) { return; }
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
                        if (scatter_range_pairs(range, cellRanges[neighbor],
                                                false, base, &local)) {
                            return;
                        }
                    }
                }
            }
        }
        return;
    }
    let body = owner - rangeCount;
    if (body >= broad.counts.x) { return; }
    let spatialClass = body_spatial_class(body);
    if (spatialClass == SPATIAL_CLASS_COMMON) { return; }
    if (spatialClass == SPATIAL_CLASS_SWEPT_OVERSIZED) {
        scatter_swept_body_pairs(body, base, &local);
        return;
    }
    for (var other = 0u; other < broad.counts.x; other += 1u) {
        if (base >= broad.counts.w
            || local >= broad.counts.w - base) { return; }
        if (other == body) { continue; }
        if (body_spatial_class(other) == SPATIAL_CLASS_PHYSICAL_OVERSIZED
            && other < body) { continue; }
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
            let limit = candidate_count_limit();
            let offset = min(ownerPairOffsets[last], limit);
            rawCandidates = offset + min(ownerPairCounts[last], limit - offset);
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
    let pair = nextContacts[index].pair;
    nextContacts[index].state = vec4<u32>(
        contactId, 0u,
        u32(metadata[pair.keyLow].w) & GENERATION_MASK,
        u32(metadata[pair.keyHigh].w) & GENERATION_MASK);
    contactOccupancy[contactId] = 1u;
    contactEvents[rank] = ContactEvent(
        pair.keyLow, pair.keyHigh, 1u, contactId,
        vec4<u32>(nextContacts[index].state.zw, 0u, 0u));
}

fn lifecycle_scatter_end_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= lifecycle_previous_count()
        || lifecycleEndPredicates[index] == 0u) { return; }
    let rank = lifecycleEndOffsets[index];
    let previous = previousContacts[index];
    contactEvents[broad.capacities.y + rank] = ContactEvent(
        previous.pair.keyLow, previous.pair.keyHigh, 2u,
        previous.state.x, vec4<u32>(previous.state.zw, 0u, 0u));
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

// Small worlds are dominated by WebGPU dispatch latency. A deterministic
// brute-force walk is cheaper than grid construction plus two radix sorts and
// already emits the canonical (minimum body, maximum body) pair order.
@compute @workgroup_size(1)
fn small_world_pairs(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    for (var index = 0u; index < 14u; index += 1u) {
        atomicStore(&telemetry[index], 0u);
    }

    var gridEntryCount = 0u;
    var occupiedCellCount = 0u;
    var oversizedCount = 0u;
    for (var body = 0u; body < broad.counts.x; body += 1u) {
        if (!body_is_alive(body)) { continue; }
        if (body_is_oversized(body)) {
            oversizedCount += 1u;
            continue;
        }
        gridEntryCount += 1u;
        var valid = false;
        let cell = body_cell(body, &valid);
        let key = encode_cell(cell);
        var firstInCell = valid;
        for (var previous = 0u; previous < body; previous += 1u) {
            if (!body_is_alive(previous) || body_is_oversized(previous)) {
                continue;
            }
            var previousValid = false;
            let previousCell = body_cell(previous, &previousValid);
            let previousKey = encode_cell(previousCell);
            if (previousValid && previousKey.x == key.x
                && previousKey.y == key.y) {
                firstInCell = false;
                break;
            }
        }
        occupiedCellCount += select(0u, 1u, firstInCell);
    }

    var rawCandidateCount = 0u;
    var sleepingCount = 0u;
    let outputCapacity = min(broad.counts.w, broad.capacities.x);
    for (var minimum = 0u; minimum < broad.counts.x; minimum += 1u) {
        for (var maximum = minimum + 1u; maximum < broad.counts.x;
             maximum += 1u) {
            if (!bodies_overlap(minimum, maximum)) { continue; }
            let output = rawCandidateCount;
            rawCandidateCount += 1u;
            if (output >= outputCapacity) { continue; }
            let sleeping = select(0u, 1u,
                (body_flags(minimum) & BODY_AWAKE) == 0u
                    || (body_flags(maximum) & BODY_AWAKE) == 0u);
            uniqueBodyPairs[output] = KeyValue(
                maximum, minimum, sleeping, output);
            sleepingCount += sleeping;
        }
    }

    let materialized = min(rawCandidateCount, broad.counts.w);
    atomicStore(&telemetry[0], gridEntryCount);
    atomicStore(&telemetry[1], occupiedCellCount);
    atomicStore(&telemetry[2], rawCandidateCount);
    atomicStore(&telemetry[3], materialized);
    atomicStore(&telemetry[4], sleepingCount);
    atomicStore(&telemetry[5], oversizedCount);
    atomicStore(&telemetry[9], select(
        0u, 1u, rawCandidateCount > broad.counts.w));
    atomicStore(&telemetry[10], select(
        0u, 1u, materialized > broad.capacities.x));
    atomicMax(&telemetry[14], gridEntryCount);
    atomicMax(&telemetry[15], occupiedCellCount);
    atomicMax(&telemetry[16], rawCandidateCount);
    atomicMax(&telemetry[17], materialized);
}

fn encoded_axes_are_neighbors(axisA : u32, axisB : u32) -> bool {
    let delta = (axisB - axisA) & CELL_MASK;
    return delta <= 1u || delta == CELL_MASK;
}

fn encoded_cells_are_neighbors(keyA : vec2<u32>, keyB : vec2<u32>) -> bool {
    if (!encoded_axes_are_neighbors(
            keyA.x & CELL_MASK, keyB.x & CELL_MASK)) { return false; }
    let yA = ((keyA.x >> 21u) & 0x7ffu) | ((keyA.y & 0x3ffu) << 11u);
    let yB = ((keyB.x >> 21u) & 0x7ffu) | ((keyB.y & 0x3ffu) << 11u);
    if (!encoded_axes_are_neighbors(yA, yB)) { return false; }
    return encoded_axes_are_neighbors(
        (keyA.y >> 10u) & CELL_MASK,
        (keyB.y >> 10u) & CELL_MASK);
}

fn cached_small_bodies_overlap(bodyA : u32, bodyB : u32,
                               keyA : vec2<u32>, keyB : vec2<u32>) -> bool {
    let flagsA = u32(smallPairSectorFlags[bodyA].w);
    let flagsB = u32(smallPairSectorFlags[bodyB].w);
    if ((flagsA & BODY_ALIVE) == 0u || (flagsB & BODY_ALIVE) == 0u
        || ((flagsA | flagsB) & BODY_AWAKE) == 0u) {
        return false;
    }
    if (smallPairUseCellCull != 0u
        && !all(keyA == vec2<u32>(SENTINEL))
        && !all(keyB == vec2<u32>(SENTINEL))) {
        // Non-oversized bodies have a combined radius no larger than one
        // cell. Therefore overlapping AABBs cannot have center cells more
        // than one toroidal bucket apart on any axis.
        if (!encoded_cells_are_neighbors(keyA, keyB)) { return false; }
    }
    var sectorDelta = vec3<i32>(0);
    for (var axis = 0u; axis < 3u; axis += 1u) {
        sectorDelta[axis] = adjacent_sector_delta(
            smallPairSectorFlags[bodyA][axis],
            smallPairSectorFlags[bodyB][axis]);
        if (abs(sectorDelta[axis]) > 1) { return false; }
    }
    let extent = smallPairPositionRadius[bodyA].w
                 + smallPairPositionRadius[bodyB].w;
    let delta = smallPairPositionRadius[bodyB].xyz
              - smallPairPositionRadius[bodyA].xyz
              + vec3<f32>(sectorDelta) * WORLD_SECTOR_SIZE;
    return all(abs(delta) <= vec3<f32>(extent));
}

struct MediumBodyProxy {
    positionRadius : vec4<f32>,
    sectorFlags : vec4<i32>,
    cellKey : vec2<u32>,
};

fn load_medium_body(body : u32) -> MediumBodyProxy {
    var proxy = MediumBodyProxy(
        vec4<f32>(0.0), vec4<i32>(0), vec2<u32>(SENTINEL));
    if (body >= broad.counts.x) { return proxy; }

    let dimensions = abs(shapes[body].dimensions_type.xyz);
    let flags = body_flags(body);
    if ((flags & BODY_ALIVE) == 0u
        || max(dimensions.x, max(dimensions.y, dimensions.z)) <= 0.0) {
        return proxy;
    }

    let radius = shape_radius(body);
    proxy.positionRadius = vec4<f32>(
        poses[body].position_invMass.xyz, radius);
    proxy.sectorFlags = vec4<i32>(
        metadata[body].xyz, bitcast<i32>(flags));
    var valid = false;
    let cell = body_cell(body, &valid);
    if (radius * 2.0 <= broad.grid.x && valid
        && coordinate_is_encodable(cell)) {
        proxy.cellKey = encode_cell(cell);
    }
    return proxy;
}

fn store_medium_body_proxy(body : u32, proxy : MediumBodyProxy) {
    let positionRadius = bitcast<vec4<u32>>(proxy.positionRadius);
    let sectorFlags = bitcast<vec4<u32>>(proxy.sectorFlags);
    gridEntries[body] = KeyValue(
        positionRadius.x, positionRadius.y,
        positionRadius.z, positionRadius.w);
    cellRanges[body] = CellRange(
        sectorFlags.x, sectorFlags.y, sectorFlags.z, sectorFlags.w);
    ownerPairCounts[body * 2u] = proxy.cellKey.x;
    ownerPairCounts[body * 2u + 1u] = proxy.cellKey.y;
}

fn load_precomputed_medium_body(body : u32) -> MediumBodyProxy {
    let positionRadius = gridEntries[body];
    let sectorFlags = cellRanges[body];
    return MediumBodyProxy(
        bitcast<vec4<f32>>(vec4<u32>(
            positionRadius.keyLow, positionRadius.keyHigh,
            positionRadius.value, positionRadius.ordinal)),
        bitcast<vec4<i32>>(vec4<u32>(
            sectorFlags.keyLow, sectorFlags.keyHigh,
            sectorFlags.firstEntry, sectorFlags.entryCount)),
        vec2<u32>(ownerPairCounts[body * 2u],
                  ownerPairCounts[body * 2u + 1u]));
}

fn medium_bodies_overlap(a : MediumBodyProxy,
                         b : MediumBodyProxy) -> bool {
    let flagsA = u32(a.sectorFlags.w);
    let flagsB = u32(b.sectorFlags.w);
    if ((flagsA & BODY_ALIVE) == 0u || (flagsB & BODY_ALIVE) == 0u
        || ((flagsA | flagsB) & BODY_AWAKE) == 0u) {
        return false;
    }
    if (!all(a.cellKey == vec2<u32>(SENTINEL))
        && !all(b.cellKey == vec2<u32>(SENTINEL))) {
        if (!encoded_cells_are_neighbors(a.cellKey, b.cellKey)) {
            return false;
        }
    }
    let extent = a.positionRadius.w + b.positionRadius.w;
    let localDelta = b.positionRadius.xyz - a.positionRadius.xyz;
    // Same-sector pairs have a zero world offset; skip three wrapped deltas.
    if (all(a.sectorFlags.xyz == b.sectorFlags.xyz)) {
        return all(abs(localDelta) <= vec3<f32>(extent));
    }
    var sectorDelta = vec3<i32>(0);
    for (var axis = 0u; axis < 3u; axis += 1u) {
        sectorDelta[axis] = adjacent_sector_delta(
            a.sectorFlags[axis], b.sectorFlags[axis]);
        if (abs(sectorDelta[axis]) > 1) { return false; }
    }
    let delta = localDelta + vec3<f32>(sectorDelta) * WORLD_SECTOR_SIZE;
    return all(abs(delta) <= vec3<f32>(extent));
}

// Rows are word-aligned so one minimum-body invocation owns every cache word
// it writes. F(n) is sum(i=1..n, ceil(i / 32)); subtracting two prefixes gives
// the start of a triangular row without atomics or a per-frame clear.
fn medium_pair_predicate_prefix_words(candidateCount : u32) -> u32 {
    let fullWords = candidateCount >> 5u;
    let remainder = candidateCount & 31u;
    return 16u * fullWords * (fullWords + 1u)
         + remainder * (fullWords + 1u);
}

fn medium_pair_predicate_row_base(minimum : u32) -> u32 {
    let maximumCandidateCount = broad.capacities.z - 1u;
    return medium_pair_predicate_prefix_words(maximumCandidateCount)
         - medium_pair_predicate_prefix_words(
               maximumCandidateCount - minimum);
}

fn medium_pair_predicate_split_words() -> u32 {
    let totalWords = medium_pair_predicate_prefix_words(
        broad.capacities.z - 1u);
    let halfWords = (totalWords + 1u) >> 1u;
    let candidateBufferWords = broad.counts.w * 4u;
    return max(halfWords, candidateBufferWords);
}

fn store_medium_pair_predicate(index : u32, split : u32, value : u32) {
    if (index < split) {
        mediumPairPredicates[index] = value;
    } else {
        mediumPairPredicatesTail[index - split] = value;
    }
}

fn load_medium_pair_predicate(index : u32, split : u32) -> u32 {
    if (index < split) { return mediumPairPredicates[index]; }
    return mediumPairPredicatesTail[index - split];
}

// For 65..256 bodies, one lane owns each minimum body ID. A single prefix sum
// gives every lane its canonical output range, so the second walk scatters in
// the exact (minimum, maximum) order without a binary rank decode or a barrier
// for every 256 candidate pairs. Body proxies stay in workgroup memory across
// both walks.
@compute @workgroup_size(256)
fn parallel_small_world_pairs(@builtin(local_invocation_id) lid : vec3<u32>) {
    let lane = lid.x;
    for (var index = lane; index < 14u; index += 256u) {
        atomicStore(&telemetry[index], 0u);
    }
    storageBarrier();
    workgroupBarrier();

    let bodyCount = broad.counts.x;
    var flags = 0u;
    var cellKey = vec2<u32>(SENTINEL);
    if (lane < bodyCount && body_is_alive(lane)) {
        flags = body_flags(lane);
        smallPairPositionRadius[lane] = vec4<f32>(
            poses[lane].position_invMass.xyz, shape_radius(lane));
        smallPairSectorFlags[lane] = vec4<i32>(
            metadata[lane].xyz, bitcast<i32>(flags));
        if (!body_is_oversized(lane)) {
            var valid = false;
            let bodyCell = body_cell(lane, &valid);
            if (valid) {
                cellKey = encode_cell(bodyCell);
            }
        }
    } else {
        smallPairPositionRadius[lane] = vec4<f32>(0.0);
        smallPairSectorFlags[lane] = vec4<i32>(0);
    }
    let oversized = select(0u, 1u,
        (flags & BODY_ALIVE) != 0u
            && all(cellKey == vec2<u32>(SENTINEL)));
    let gridEntry = select(0u, 1u,
        (flags & BODY_ALIVE) != 0u && oversized == 0u);
    let hash = (cellKey.x * 0x9e3779b9u)
             ^ (cellKey.y * 0x85ebca6bu);
    smallPairFlags[lane] = gridEntry | (oversized << 1u);
    smallPairOffsets[lane] = select(0u, 1u << (hash >> 27u),
        gridEntry != 0u);
    smallPairCellKeys[lane] = cellKey;
    workgroupBarrier();

    if (lane == 0u) {
        var gridEntryCount = 0u;
        var oversizedCount = 0u;
        var cellBloom = 0u;
        for (var body = 0u; body < bodyCount; body += 1u) {
            let bodyStats = smallPairFlags[body];
            gridEntryCount += bodyStats & 1u;
            oversizedCount += (bodyStats >> 1u) & 1u;
            cellBloom |= smallPairOffsets[body];
        }
        atomicStore(&telemetry[0], gridEntryCount);
        atomicStore(&telemetry[5], oversizedCount);
        atomicMax(&telemetry[14], gridEntryCount);
        // A small bloom filter chooses between two equivalent paths. Dispersed
        // worlds fuse exact occupied-cell counting into the pair walk; compact
        // worlds retain the cheap early-exit scan and skip the cell cull.
        smallPairUseCellCull = select(
            0u, 1u, countOneBits(cellBloom) > 8u);
    }
    workgroupBarrier();

    var pairCount = 0u;
    var cellRepresentative = !all(cellKey == vec2<u32>(SENTINEL));
    if (lane < bodyCount) {
        if (smallPairUseCellCull == 0u && cellRepresentative) {
            for (var previous = 0u; previous < lane; previous += 1u) {
                if (all(smallPairCellKeys[previous] == cellKey)) {
                    cellRepresentative = false;
                    break;
                }
            }
        }
        for (var maximum = lane + 1u; maximum < bodyCount; maximum += 1u) {
            let maximumKey = smallPairCellKeys[maximum];
            if (smallPairUseCellCull != 0u && cellRepresentative
                && all(maximumKey == cellKey)) {
                cellRepresentative = false;
            }
            pairCount += select(0u, 1u,
                cached_small_bodies_overlap(
                    lane, maximum, cellKey, maximumKey));
        }
    }
    smallPairFlags[lane] = pairCount | select(
        0u, 0x80000000u, cellRepresentative);
    workgroupBarrier();

    if (lane == 0u) {
        var running = 0u;
        var occupiedCellCount = 0u;
        for (var body = 0u; body < bodyCount; body += 1u) {
            smallPairOffsets[body] = running;
            let bodyResult = smallPairFlags[body];
            running += bodyResult & 0x7fffffffu;
            occupiedCellCount += bodyResult >> 31u;
        }
        smallPairOutputBase = running;
        atomicStore(&telemetry[1], occupiedCellCount);
        atomicMax(&telemetry[15], occupiedCellCount);
    }
    workgroupBarrier();

    let outputCapacity = min(broad.counts.w, broad.capacities.x);
    var localRank = 0u;
    var sleepingCount = 0u;
    if (lane < bodyCount
        && (smallPairFlags[lane] & 0x7fffffffu) != 0u) {
        let minimumFlags = u32(smallPairSectorFlags[lane].w);
        for (var maximum = lane + 1u; maximum < bodyCount; maximum += 1u) {
            if (!cached_small_bodies_overlap(
                    lane, maximum, cellKey,
                    smallPairCellKeys[maximum])) { continue; }
            let output = smallPairOffsets[lane] + localRank;
            if (output < outputCapacity) {
                let maximumFlags = u32(smallPairSectorFlags[maximum].w);
                let sleeping = select(0u, 1u,
                    (minimumFlags & BODY_AWAKE) == 0u
                        || (maximumFlags & BODY_AWAKE) == 0u);
                uniqueBodyPairs[output] = KeyValue(
                    maximum, lane, sleeping, output);
                sleepingCount += sleeping;
            }
            localRank += 1u;
        }
    }
    smallPairFlags[lane] = sleepingCount;
    storageBarrier();
    workgroupBarrier();

    if (lane == 0u) {
        let rawCandidateCount = smallPairOutputBase;
        let materialized = min(rawCandidateCount, broad.counts.w);
        var totalSleepingCount = 0u;
        for (var body = 0u; body < bodyCount; body += 1u) {
            totalSleepingCount += smallPairFlags[body];
        }
        atomicStore(&telemetry[2], rawCandidateCount);
        atomicStore(&telemetry[3], materialized);
        atomicStore(&telemetry[4], totalSleepingCount);
        atomicStore(&telemetry[9], select(
            0u, 1u, rawCandidateCount > broad.counts.w));
        atomicStore(&telemetry[10], select(
            0u, 1u, materialized > broad.capacities.x));
        atomicMax(&telemetry[16], rawCandidateCount);
        atomicMax(&telemetry[17], materialized);
    }
}

// For 257..512 bodies, keep pair generation in one workgroup instead of
// crossing the fixed-cost grid/radix cliff. Each 256-body minimum-ID tile is
// counted and scattered in canonical order. Proxies are streamed from storage
// so the path remains inside WebGPU's portable 16 KiB workgroup-memory limit.
@compute @workgroup_size(256)
fn medium_world_pairs(@builtin(local_invocation_id) lid : vec3<u32>) {
    let lane = lid.x;
    for (var index = lane; index < 14u; index += 256u) {
        atomicStore(&telemetry[index], 0u);
    }
    if (lane == 0u) { smallPairOutputBase = 0u; }
    storageBarrier();
    workgroupBarrier();

    let bodyCount = broad.counts.x;
    let outputCapacity = min(broad.counts.w, broad.capacities.x);
    for (var base = 0u; base < bodyCount; base += 256u) {
        let minimum = base + lane;
        let minimumProxy = load_medium_body(minimum);
        let minimumFlags = u32(minimumProxy.sectorFlags.w);
        let cellValid = !all(
            minimumProxy.cellKey == vec2<u32>(SENTINEL));
        var cellRepresentative = cellValid;
        var pairCount = 0u;
        if (minimum < bodyCount) {
            for (var maximum = minimum + 1u; maximum < bodyCount;
                 maximum += 1u) {
                let maximumProxy = load_medium_body(maximum);
                if (cellRepresentative
                    && all(maximumProxy.cellKey == minimumProxy.cellKey)) {
                    cellRepresentative = false;
                }
                pairCount += select(0u, 1u,
                    medium_bodies_overlap(minimumProxy, maximumProxy));
            }
        }

        let alive = (minimumFlags & BODY_ALIVE) != 0u;
        let gridEntry = select(0u, 1u, alive && cellValid);
        let oversized = select(0u, 1u, alive && !cellValid);
        smallPairFlags[lane] = pairCount
            | (gridEntry << 29u)
            | (select(0u, 1u, cellRepresentative) << 30u)
            | (oversized << 31u);
        workgroupBarrier();

        if (lane == 0u) {
            var running = 0u;
            var gridEntryCount = 0u;
            var occupiedCellCount = 0u;
            var oversizedCount = 0u;
            let chunkSize = min(256u, bodyCount - base);
            for (var index = 0u; index < chunkSize; index += 1u) {
                let packed = smallPairFlags[index];
                smallPairOffsets[index] = smallPairOutputBase + running;
                running += packed & 0x1fffffffu;
                gridEntryCount += (packed >> 29u) & 1u;
                occupiedCellCount += (packed >> 30u) & 1u;
                oversizedCount += packed >> 31u;
            }
            smallPairOutputBase += running;
            atomicAdd(&telemetry[0], gridEntryCount);
            atomicAdd(&telemetry[1], occupiedCellCount);
            atomicAdd(&telemetry[5], oversizedCount);
        }
        workgroupBarrier();

        var localRank = 0u;
        var sleepingCount = 0u;
        if (minimum < bodyCount && pairCount != 0u
            && smallPairOffsets[lane] < outputCapacity) {
            for (var maximum = minimum + 1u; maximum < bodyCount;
                 maximum += 1u) {
                if (smallPairOffsets[lane] + localRank >= outputCapacity) {
                    break;
                }
                let maximumProxy = load_medium_body(maximum);
                if (!medium_bodies_overlap(
                        minimumProxy, maximumProxy)) { continue; }
                let output = smallPairOffsets[lane] + localRank;
                let maximumFlags = u32(maximumProxy.sectorFlags.w);
                let sleeping = select(0u, 1u,
                    (minimumFlags & BODY_AWAKE) == 0u
                        || (maximumFlags & BODY_AWAKE) == 0u);
                uniqueBodyPairs[output] = KeyValue(
                    maximum, minimum, sleeping, output);
                sleepingCount += sleeping;
                localRank += 1u;
            }
        }
        smallPairFlags[lane] = sleepingCount;
        storageBarrier();
        workgroupBarrier();

        if (lane == 0u) {
            var chunkSleepingCount = 0u;
            let chunkSize = min(256u, bodyCount - base);
            for (var index = 0u; index < chunkSize; index += 1u) {
                chunkSleepingCount += smallPairFlags[index];
            }
            atomicAdd(&telemetry[4], chunkSleepingCount);
        }
        workgroupBarrier();
    }

    if (lane == 0u) {
        let rawCandidateCount = smallPairOutputBase;
        let materialized = min(rawCandidateCount, broad.counts.w);
        let gridEntryCount = atomicLoad(&telemetry[0]);
        let occupiedCellCount = atomicLoad(&telemetry[1]);
        atomicStore(&telemetry[2], rawCandidateCount);
        atomicStore(&telemetry[3], materialized);
        atomicStore(&telemetry[9], select(
            0u, 1u, rawCandidateCount > broad.counts.w));
        atomicStore(&telemetry[10], select(
            0u, 1u, materialized > broad.capacities.x));
        atomicMax(&telemetry[14], gridEntryCount);
        atomicMax(&telemetry[15], occupiedCellCount);
        atomicMax(&telemetry[16], rawCandidateCount);
        atomicMax(&telemetry[17], materialized);
    }
}

// Beyond two 256-lane tiles, keeping every minimum body in one workgroup
// serializes the triangular pair walk. Count one minimum body per invocation,
// scan those counts, then scatter from the same canonical ranges. This keeps
// the low-dispatch direct-pair path while allowing several workgroups to cover
// medium-sized browser worlds concurrently.
fn precompute_medium_body_proxies_impl(gid : vec3<u32>) {
    if (gid.x < broad.counts.x) {
        store_medium_body_proxy(gid.x, load_medium_body(gid.x));
    }
}

@compute @workgroup_size(64)
fn precompute_medium_body_proxies_64(
    @builtin(global_invocation_id) gid : vec3<u32>) {
    precompute_medium_body_proxies_impl(gid);
}
@compute @workgroup_size(128)
fn precompute_medium_body_proxies_128(
    @builtin(global_invocation_id) gid : vec3<u32>) {
    precompute_medium_body_proxies_impl(gid);
}
@compute @workgroup_size(256)
fn precompute_medium_body_proxies_256(
    @builtin(global_invocation_id) gid : vec3<u32>) {
    precompute_medium_body_proxies_impl(gid);
}

fn parallel_medium_world_pair_counts_impl(gid : vec3<u32>,
                                          lid : vec3<u32>) {
    let minimum = gid.x;
    let bodyCount = broad.counts.x;
    let validMinimum = minimum < bodyCount;
    var minimumProxy = MediumBodyProxy(
        vec4<f32>(0.0), vec4<i32>(0), vec2<u32>(SENTINEL));
    if (validMinimum) {
        minimumProxy = load_precomputed_medium_body(minimum);
    }
    let minimumFlags = u32(minimumProxy.sectorFlags.w);
    let cellValid = !all(
        minimumProxy.cellKey == vec2<u32>(SENTINEL));
    var cellRepresentative = cellValid;
    var pairCount = 0u;
    var predicateRowBase = 0u;
    if (validMinimum) {
        predicateRowBase = medium_pair_predicate_row_base(minimum);
    }
    let predicateSplit = medium_pair_predicate_split_words();
    var predicateWord = 0u;
    let tileSize = broad.capacities.w;
    for (var base = 0u; base < bodyCount; base += tileSize) {
        let loadIndex = base + lid.x;
        var loaded = MediumBodyProxy(
            vec4<f32>(0.0), vec4<i32>(0), vec2<u32>(SENTINEL));
        if (loadIndex < bodyCount) {
            loaded = load_precomputed_medium_body(loadIndex);
        }
        smallPairPositionRadius[lid.x] = loaded.positionRadius;
        smallPairSectorFlags[lid.x] = loaded.sectorFlags;
        smallPairCellKeys[lid.x] = loaded.cellKey;
        workgroupBarrier();
        if (validMinimum) {
            let tileCount = min(tileSize, bodyCount - base);
            for (var local = 0u; local < tileCount; local += 1u) {
                let maximum = base + local;
                if (maximum <= minimum) { continue; }
                let maximumProxy = MediumBodyProxy(
                    smallPairPositionRadius[local],
                    smallPairSectorFlags[local],
                    smallPairCellKeys[local]);
                if (cellRepresentative
                    && all(maximumProxy.cellKey == minimumProxy.cellKey)) {
                    cellRepresentative = false;
                }
                let overlaps = medium_bodies_overlap(
                    minimumProxy, maximumProxy);
                let relative = maximum - minimum - 1u;
                predicateWord |= select(
                    0u, 1u << (relative & 31u), overlaps);
                if ((relative & 31u) == 31u
                    || maximum + 1u == bodyCount) {
                    store_medium_pair_predicate(
                        predicateRowBase + (relative >> 5u),
                        predicateSplit, predicateWord);
                    predicateWord = 0u;
                }
                pairCount += select(0u, 1u, overlaps);
            }
        }
        workgroupBarrier();
    }
    if (!validMinimum) { return; }

    bodyEntryCounts[minimum] = pairCount;
    let alive = (minimumFlags & BODY_ALIVE) != 0u;
    if (alive && cellValid) {
        atomicAdd(&telemetry[0], 1u);
        if (cellRepresentative) { atomicAdd(&telemetry[1], 1u); }
    } else if (alive) {
        atomicAdd(&telemetry[5], 1u);
    }
}

@compute @workgroup_size(64)
fn parallel_medium_world_pair_counts_64(
    @builtin(global_invocation_id) gid : vec3<u32>,
    @builtin(local_invocation_id) lid : vec3<u32>) {
    parallel_medium_world_pair_counts_impl(gid, lid);
}
@compute @workgroup_size(128)
fn parallel_medium_world_pair_counts_128(
    @builtin(global_invocation_id) gid : vec3<u32>,
    @builtin(local_invocation_id) lid : vec3<u32>) {
    parallel_medium_world_pair_counts_impl(gid, lid);
}
@compute @workgroup_size(256)
fn parallel_medium_world_pair_counts_256(
    @builtin(global_invocation_id) gid : vec3<u32>,
    @builtin(local_invocation_id) lid : vec3<u32>) {
    parallel_medium_world_pair_counts_impl(gid, lid);
}

fn parallel_medium_world_pair_scatter_impl(gid : vec3<u32>) {
    let minimum = gid.x;
    let bodyCount = broad.counts.x;
    let validMinimum = minimum < bodyCount;

    if (validMinimum && minimum == 0u) {
        let last = bodyCount - 1u;
        let rawCandidateCount =
            bodyEntryOffsets[last] + bodyEntryCounts[last];
        let materialized = min(rawCandidateCount, broad.counts.w);
        let gridEntryCount = atomicLoad(&telemetry[0]);
        let occupiedCellCount = atomicLoad(&telemetry[1]);
        atomicStore(&telemetry[2], rawCandidateCount);
        atomicStore(&telemetry[3], materialized);
        atomicStore(&telemetry[9], select(
            0u, 1u, rawCandidateCount > broad.counts.w));
        atomicStore(&telemetry[10], select(
            0u, 1u, materialized > broad.capacities.x));
        atomicMax(&telemetry[14], gridEntryCount);
        atomicMax(&telemetry[15], occupiedCellCount);
        atomicMax(&telemetry[16], rawCandidateCount);
        atomicMax(&telemetry[17], materialized);
    }

    if (!validMinimum) { return; }
    let pairCount = bodyEntryCounts[minimum];
    let outputBase = bodyEntryOffsets[minimum];
    let outputCapacity = min(broad.counts.w, broad.capacities.x);
    if (pairCount == 0u || outputBase >= outputCapacity) { return; }

    let predicateRowBase = medium_pair_predicate_row_base(minimum);
    let predicateSplit = medium_pair_predicate_split_words();
    let minimumFlags = u32(cellRanges[minimum].entryCount);
    let predicateWordCount =
        (bodyCount - minimum - 1u + 31u) >> 5u;
    var localRank = 0u;
    var sleepingCount = 0u;
    var outputFull = false;
    for (var wordIndex = 0u;
         wordIndex < predicateWordCount && !outputFull;
         wordIndex += 1u) {
        var predicates = load_medium_pair_predicate(
            predicateRowBase + wordIndex, predicateSplit);
        while (predicates != 0u) {
            let bit = firstTrailingBit(predicates);
            let maximum = minimum + 1u + wordIndex * 32u + bit;
            let output = outputBase + localRank;
            let maximumFlags = u32(cellRanges[maximum].entryCount);
            let sleeping = select(0u, 1u,
                (minimumFlags & BODY_AWAKE) == 0u
                    || (maximumFlags & BODY_AWAKE) == 0u);
            uniqueBodyPairs[output] = KeyValue(
                maximum, minimum, sleeping, output);
            sleepingCount += sleeping;
            localRank += 1u;
            outputFull = outputBase + localRank >= outputCapacity;
            if (outputFull) { break; }
            predicates = predicates & (predicates - 1u);
        }
    }
    if (sleepingCount != 0u) {
        atomicAdd(&telemetry[4], sleepingCount);
    }
}

@compute @workgroup_size(64)
fn parallel_medium_world_pair_scatter_64(
    @builtin(global_invocation_id) gid : vec3<u32>) {
    parallel_medium_world_pair_scatter_impl(gid);
}
@compute @workgroup_size(128)
fn parallel_medium_world_pair_scatter_128(
    @builtin(global_invocation_id) gid : vec3<u32>) {
    parallel_medium_world_pair_scatter_impl(gid);
}
@compute @workgroup_size(256)
fn parallel_medium_world_pair_scatter_256(
    @builtin(global_invocation_id) gid : vec3<u32>) {
    parallel_medium_world_pair_scatter_impl(gid);
}

fn small_world_lifecycle_impl() {
    let currentCount = lifecycle_current_count();
    let previousCount = lifecycle_previous_count();

    // Occupancy from the previous tick contains exactly the previous contact
    // IDs. Clear those IDs, then retain IDs for pairs that survived this tick.
    for (var index = 0u; index < previousCount; index += 1u) {
        let contactId = previousContacts[index].state.x;
        if (contactId < broad.capacities.y) {
            contactOccupancy[contactId] = 0u;
        }
    }

    var persistentCount = 0u;
    var beginCount = 0u;
    for (var index = 0u; index < currentCount; index += 1u) {
        let pair = uniqueBodyPairs[index];
        let previousIndex = find_previous_contact(pair);
        nextContacts[index].pair = pair;
        if (previousIndex != SENTINEL) {
            var state = previousContacts[previousIndex].state;
            state.y += 1u;
            if (state.x < broad.capacities.y) {
                nextContacts[index].state = state;
                contactOccupancy[state.x] = 1u;
                persistentCount += 1u;
                continue;
            }
        }
        nextContacts[index].state = vec4<u32>(
            SENTINEL, 0u, SENTINEL, 0u);
        beginCount += 1u;
    }

    var endCount = 0u;
    for (var index = 0u; index < previousCount; index += 1u) {
        let previous = previousContacts[index];
        if (find_current_pair(previous) != SENTINEL) { continue; }
        contactEvents[broad.capacities.y + endCount] = ContactEvent(
            previous.pair.keyLow, previous.pair.keyHigh, 2u,
            previous.state.x,
            vec4<u32>(previous.state.zw, 0u, 0u));
        endCount += 1u;
    }

    let freeCount = broad.capacities.y - persistentCount;
    var beginRank = 0u;
    var nextFreeId = 0u;
    for (var index = 0u; index < currentCount; index += 1u) {
        if (nextContacts[index].state.x != SENTINEL) { continue; }
        let pair = nextContacts[index].pair;
        while (nextFreeId < broad.capacities.y
            && contactOccupancy[nextFreeId] != 0u) {
            nextFreeId += 1u;
        }
        if (nextFreeId < broad.capacities.y) {
            nextContacts[index].state = vec4<u32>(
                nextFreeId, 0u,
                u32(metadata[pair.keyLow].w) & GENERATION_MASK,
                u32(metadata[pair.keyHigh].w) & GENERATION_MASK);
            contactOccupancy[nextFreeId] = 1u;
            contactEvents[beginRank] = ContactEvent(
                pair.keyLow, pair.keyHigh, 1u, nextFreeId,
                vec4<u32>(nextContacts[index].state.zw, 0u, 0u));
            nextFreeId += 1u;
        }
        beginRank += 1u;
    }

    lifecycleState[0] = currentCount;
    lifecycleState[1] += 1u;
    lifecycleState[2] = currentCount;
    lifecycleState[3] = freeCount;
    atomicStore(&telemetry[6], currentCount);
    atomicStore(&telemetry[7], beginCount);
    atomicStore(&telemetry[8], endCount);
    atomicStore(&telemetry[11], select(0u, 1u,
        atomicLoad(&telemetry[3]) > broad.capacities.y
            || beginCount > freeCount));
    atomicStore(&telemetry[12], select(0u, 1u,
        beginCount > broad.capacities.y || endCount > broad.capacities.y));
    atomicMax(&telemetry[18], currentCount);
    atomicStore(&telemetry[19], lifecycleState[1]);
    atomicMax(&telemetry[20], beginCount + endCount);
}

@compute @workgroup_size(1)
fn small_world_lifecycle(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    small_world_lifecycle_impl();
}

// Select the sparse serial lifecycle on the GPU, after pair generation has
// published its exact dynamic count. The fallback dispatch arguments keep the
// existing parallel scans for dense contact sets.
@compute @workgroup_size(1)
fn hybrid_lifecycle(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    let useSmall = lifecycle_current_count() <= SMALL_LIFECYCLE_CONTACT_LIMIT
        && lifecycle_previous_count() <= SMALL_LIFECYCLE_CONTACT_LIMIT;
    store_sort_dispatch(18u, select(
        broad.capacities.y, 0u, useSmall));
    // Keep the selector below WebGPU's guaranteed eight-storage-buffer
    // limit. The serial lifecycle is launched indirectly with this separate
    // argument instead of being fused into this kernel.
    sortDispatchArgs[24] = select(0u, 1u, useSmall);
    sortDispatchArgs[25] = 1u;
    sortDispatchArgs[26] = 1u;
}
