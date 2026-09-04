struct Params {
    counts : vec4<u32>,
};

struct KeyValue {
    keyLow : u32,
    keyHigh : u32,
    value : u32,
    ordinal : u32,
};

@group(0) @binding(0) var<storage, read> scanInput : array<u32>;
@group(0) @binding(1) var<storage, read_write> scanOutput : array<u32>;
@group(0) @binding(2) var<storage, read_write> scanBlockSums : array<u32>;
@group(0) @binding(3) var<storage, read_write> scanBlockPrefix : array<u32>;
@group(0) @binding(4) var<uniform> params : Params;
@group(0) @binding(5) var<storage, read> scanDynamicCounts : array<u32>;

@group(0) @binding(10) var<storage, read> compactValues : array<u32>;
@group(0) @binding(11) var<storage, read> compactPredicates : array<u32>;
@group(0) @binding(12) var<storage, read> compactOffsets : array<u32>;
@group(0) @binding(13) var<storage, read_write> compactOutput : array<u32>;
@group(0) @binding(14) var<storage, read_write> operationResult : array<u32>;

@group(0) @binding(20) var<storage, read> radixInput : array<KeyValue>;
@group(0) @binding(21) var<storage, read_write> radixOutput : array<KeyValue>;
@group(0) @binding(22) var<storage, read_write> radixHistogram : array<atomic<u32>>;
@group(0) @binding(23) var<storage, read_write> radixOffsets : array<u32>;
@group(0) @binding(24) var<storage, read_write> radixDigitBases : array<u32>;
@group(0) @binding(25) var<storage, read> radixDynamicCounts : array<u32>;

@group(0) @binding(30) var<storage, read> uniqueInput : array<KeyValue>;
@group(0) @binding(31) var<storage, read_write> uniqueOutput : array<KeyValue>;
@group(0) @binding(32) var<storage, read_write> uniquePredicates : array<u32>;
@group(0) @binding(33) var<storage, read> uniqueOffsets : array<u32>;

@group(0) @binding(40) var<storage, read> mergePrevious : array<KeyValue>;
@group(0) @binding(41) var<storage, read> mergeCurrent : array<KeyValue>;
@group(0) @binding(42) var<storage, read_write> mergeOutput : array<KeyValue>;
@group(0) @binding(43) var<storage, read_write> mergeTags : array<u32>;

@group(0) @binding(50) var<storage, read> assignRequests : array<u32>;
@group(0) @binding(51) var<storage, read> assignOffsets : array<u32>;
@group(0) @binding(52) var<storage, read> assignFreeIds : array<u32>;
@group(0) @binding(53) var<storage, read_write> assignedIds : array<u32>;

var<workgroup> scanScratch : array<u32, 256>;
var<workgroup> radixHistogramScratch : array<atomic<u32>, 256>;
var<workgroup> radixDigitScan : array<u32, 256>;
var<workgroup> radixLocalCounts : array<u32, 2048>;

fn scan_count() -> u32 {
    if (params.counts.y == 0xffffffffu) { return params.counts.x; }
    let word = params.counts.y & 0x00ffffffu;
    let shift = (params.counts.y >> 24u) & 1u;
    let maximumBeforeShift = params.counts.x >> shift;
    return min(scanDynamicCounts[word], maximumBeforeShift) << shift;
}

fn scan_block_count() -> u32 {
    let count = scan_count();
    return (count + params.counts.z - 1u) / params.counts.z;
}

fn scan_sum(a : u32, b : u32) -> u32 {
    let limit = params.counts.w;
    if (limit == 0xffffffffu) { return a + b; }
    let sum = a + b;
    // An overflowing u32 sum is necessarily above every finite clamp.  This
    // keeps the common sparse case to one add, one comparison, and one min.
    return select(min(sum, limit), limit, sum < a);
}

fn scan_blocks_impl(gid : vec3<u32>, lid : vec3<u32>, group : vec3<u32>,
                    groupSize : u32) {
    let index = gid.x;
    var value = 0u;
    if (index < scan_count()) { value = scanInput[index]; }
    scanScratch[lid.x] = value;
    workgroupBarrier();
    var offset = 1u;
    while (offset < groupSize) {
        var addend = 0u;
        if (lid.x >= offset) { addend = scanScratch[lid.x - offset]; }
        workgroupBarrier();
        scanScratch[lid.x] = scan_sum(scanScratch[lid.x], addend);
        workgroupBarrier();
        offset <<= 1u;
    }
    if (index < scan_count()) {
        scanOutput[index] = scanScratch[lid.x] - value;
    }
    if (lid.x + 1u == groupSize) {
        scanBlockSums[group.x] = scanScratch[lid.x];
    }
}

@compute @workgroup_size(64)
fn scan_blocks_64(@builtin(global_invocation_id) gid : vec3<u32>,
                  @builtin(local_invocation_id) lid : vec3<u32>,
                  @builtin(workgroup_id) group : vec3<u32>) {
    scan_blocks_impl(gid, lid, group, 64u);
}

@compute @workgroup_size(128)
fn scan_blocks_128(@builtin(global_invocation_id) gid : vec3<u32>,
                   @builtin(local_invocation_id) lid : vec3<u32>,
                   @builtin(workgroup_id) group : vec3<u32>) {
    scan_blocks_impl(gid, lid, group, 128u);
}

@compute @workgroup_size(256)
fn scan_blocks_256(@builtin(global_invocation_id) gid : vec3<u32>,
                   @builtin(local_invocation_id) lid : vec3<u32>,
                   @builtin(workgroup_id) group : vec3<u32>) {
    scan_blocks_impl(gid, lid, group, 256u);
}

@compute @workgroup_size(1)
fn scan_prefix(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    var sum = 0u;
    for (var block = 0u; block < scan_block_count(); block += 1u) {
        scanBlockPrefix[block] = sum;
        sum = scan_sum(sum, scanBlockSums[block]);
    }
}

fn scan_add_impl(gid : vec3<u32>, group : vec3<u32>) {
    if (gid.x < scan_count()) {
        scanOutput[gid.x] = scan_sum(
            scanOutput[gid.x], scanBlockPrefix[group.x]);
    }
}

@compute @workgroup_size(64)
fn scan_add_64(@builtin(global_invocation_id) gid : vec3<u32>,
               @builtin(workgroup_id) group : vec3<u32>) {
    scan_add_impl(gid, group);
}

@compute @workgroup_size(128)
fn scan_add_128(@builtin(global_invocation_id) gid : vec3<u32>,
                @builtin(workgroup_id) group : vec3<u32>) {
    scan_add_impl(gid, group);
}

@compute @workgroup_size(256)
fn scan_add_256(@builtin(global_invocation_id) gid : vec3<u32>,
                @builtin(workgroup_id) group : vec3<u32>) {
    scan_add_impl(gid, group);
}

fn compact_scatter_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index < params.counts.x && compactPredicates[index] != 0u) {
        compactOutput[compactOffsets[index]] = compactValues[index];
    }
}

@compute @workgroup_size(64)
fn compact_scatter_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    compact_scatter_impl(gid);
}
@compute @workgroup_size(128)
fn compact_scatter_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    compact_scatter_impl(gid);
}
@compute @workgroup_size(256)
fn compact_scatter_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    compact_scatter_impl(gid);
}

@compute @workgroup_size(1)
fn compact_finalize(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    var count = 0u;
    if (params.counts.x != 0u) {
        let last = params.counts.x - 1u;
        count = compactOffsets[last] + select(0u, 1u,
                                              compactPredicates[last] != 0u);
    }
    operationResult[0] = count;
    operationResult[1] = 0u;
    operationResult[2] = 0u;
    operationResult[3] = 0u;
}

fn radix_digit(record : KeyValue) -> u32 {
    let passIndex = params.counts.w;
    let word = select(record.keyLow, record.keyHigh, passIndex >= 4u);
    return (word >> ((passIndex & 3u) * 8u)) & 255u;
}

fn radix_count() -> u32 {
    if (params.counts.y == 0xffffffffu) { return params.counts.x; }
    let word = params.counts.y & 0x00ffffffu;
    let shift = (params.counts.y >> 24u) & 1u;
    let maximumBeforeShift = params.counts.x >> shift;
    return min(radixDynamicCounts[word], maximumBeforeShift) << shift;
}

fn radix_block_count() -> u32 {
    let count = radix_count();
    return (count + params.counts.z - 1u) / params.counts.z;
}

fn radix_histogram_impl(gid : vec3<u32>, lid : vec3<u32>,
                        group : vec3<u32>, groupSize : u32) {
    for (var digit = lid.x; digit < 256u; digit += groupSize) {
        atomicStore(&radixHistogramScratch[digit], 0u);
    }
    workgroupBarrier();
    if (gid.x < radix_count()) {
        atomicAdd(&radixHistogramScratch[radix_digit(radixInput[gid.x])], 1u);
    }
    workgroupBarrier();
    for (var digit = lid.x; digit < 256u; digit += groupSize) {
        atomicStore(&radixHistogram[group.x * 256u + digit],
                    atomicLoad(&radixHistogramScratch[digit]));
    }
}

@compute @workgroup_size(64)
fn radix_histogram_64(@builtin(global_invocation_id) gid : vec3<u32>,
                      @builtin(local_invocation_id) lid : vec3<u32>,
                      @builtin(workgroup_id) group : vec3<u32>) {
    radix_histogram_impl(gid, lid, group, 64u);
}
@compute @workgroup_size(128)
fn radix_histogram_128(@builtin(global_invocation_id) gid : vec3<u32>,
                       @builtin(local_invocation_id) lid : vec3<u32>,
                       @builtin(workgroup_id) group : vec3<u32>) {
    radix_histogram_impl(gid, lid, group, 128u);
}
@compute @workgroup_size(256)
fn radix_histogram_256(@builtin(global_invocation_id) gid : vec3<u32>,
                       @builtin(local_invocation_id) lid : vec3<u32>,
                       @builtin(workgroup_id) group : vec3<u32>) {
    radix_histogram_impl(gid, lid, group, 256u);
}

@compute @workgroup_size(256)
fn radix_prefix(@builtin(local_invocation_id) lid : vec3<u32>) {
    let digit = lid.x;
    var running = 0u;
    for (var block = 0u; block < radix_block_count(); block += 1u) {
        let index = block * 256u + digit;
        radixOffsets[index] = running;
        running += atomicLoad(&radixHistogram[index]);
    }
    radixDigitScan[digit] = running;
    workgroupBarrier();
    var offset = 1u;
    while (offset < 256u) {
        var addend = 0u;
        if (digit >= offset) { addend = radixDigitScan[digit - offset]; }
        workgroupBarrier();
        radixDigitScan[digit] += addend;
        workgroupBarrier();
        offset <<= 1u;
    }
    radixDigitBases[digit] = radixDigitScan[digit] - running;
}

fn radix_scatter_impl(lid : vec3<u32>, group : vec3<u32>, groupSize : u32) {
    let workerCount = 8u;
    for (var index = lid.x; index < workerCount * 256u;
         index += groupSize) {
        radixLocalCounts[index] = 0u;
    }
    workgroupBarrier();
    let blockStart = group.x * groupSize;
    let blockEnd = min(blockStart + groupSize, radix_count());
    let chunkSize = (groupSize + workerCount - 1u) / workerCount;
    if (lid.x < workerCount) {
        let first = min(blockStart + lid.x * chunkSize, blockEnd);
        let last = min(first + chunkSize, blockEnd);
        let row = lid.x * 256u;
        for (var index = first; index < last; index += 1u) {
            let digit = radix_digit(radixInput[index]);
            radixLocalCounts[row + digit] += 1u;
        }
    }
    workgroupBarrier();
    // Each digit has one invocation owner for all worker rows. Keep its
    // exclusive prefix private instead of round-tripping through shared
    // atomics and synchronizing the whole workgroup after every row.
    for (var digit = lid.x; digit < 256u; digit += groupSize) {
        var prefix = 0u;
        for (var worker = 0u; worker < workerCount; worker += 1u) {
            let slot = worker * 256u + digit;
            let count = radixLocalCounts[slot];
            radixLocalCounts[slot] = prefix;
            prefix += count;
        }
    }
    // Scatter workers consume prefixes produced by other digit owners.
    workgroupBarrier();
    if (lid.x < workerCount) {
        let first = min(blockStart + lid.x * chunkSize, blockEnd);
        let last = min(first + chunkSize, blockEnd);
        let row = lid.x * 256u;
        for (var index = first; index < last; index += 1u) {
            let record = radixInput[index];
            let digit = radix_digit(record);
            let localRank = radixLocalCounts[row + digit];
            radixLocalCounts[row + digit] = localRank + 1u;
            let output = radixDigitBases[digit]
                + radixOffsets[group.x * 256u + digit] + localRank;
            radixOutput[output] = record;
        }
    }
}

@compute @workgroup_size(64)
fn radix_scatter_64(@builtin(local_invocation_id) lid : vec3<u32>,
                    @builtin(workgroup_id) group : vec3<u32>) {
    radix_scatter_impl(lid, group, 64u);
}
@compute @workgroup_size(128)
fn radix_scatter_128(@builtin(local_invocation_id) lid : vec3<u32>,
                     @builtin(workgroup_id) group : vec3<u32>) {
    radix_scatter_impl(lid, group, 128u);
}
@compute @workgroup_size(256)
fn radix_scatter_256(@builtin(local_invocation_id) lid : vec3<u32>,
                     @builtin(workgroup_id) group : vec3<u32>) {
    radix_scatter_impl(lid, group, 256u);
}

fn keys_equal(a : KeyValue, b : KeyValue) -> bool {
    return a.keyLow == b.keyLow
        && (params.counts.w == 1u || a.keyHigh == b.keyHigh);
}

fn unique_mark_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= params.counts.x) { return; }
    var keep = index == 0u;
    if (index != 0u) {
        keep = !keys_equal(uniqueInput[index - 1u], uniqueInput[index]);
    }
    uniquePredicates[index] = select(0u, 1u, keep);
}

@compute @workgroup_size(64)
fn unique_mark_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    unique_mark_impl(gid);
}
@compute @workgroup_size(128)
fn unique_mark_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    unique_mark_impl(gid);
}
@compute @workgroup_size(256)
fn unique_mark_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    unique_mark_impl(gid);
}

fn unique_scatter_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index < params.counts.x && uniquePredicates[index] != 0u) {
        uniqueOutput[uniqueOffsets[index]] = uniqueInput[index];
    }
}

@compute @workgroup_size(64)
fn unique_scatter_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    unique_scatter_impl(gid);
}
@compute @workgroup_size(128)
fn unique_scatter_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    unique_scatter_impl(gid);
}
@compute @workgroup_size(256)
fn unique_scatter_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    unique_scatter_impl(gid);
}

@compute @workgroup_size(1)
fn unique_finalize(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    var count = 0u;
    if (params.counts.x != 0u) {
        let last = params.counts.x - 1u;
        count = uniqueOffsets[last] + uniquePredicates[last];
    }
    operationResult[0] = count;
    operationResult[1] = 0u;
    operationResult[2] = 0u;
    operationResult[3] = 0u;
}

fn key_less(a : KeyValue, b : KeyValue) -> bool {
    return a.keyHigh < b.keyHigh
        || (a.keyHigh == b.keyHigh && a.keyLow < b.keyLow);
}

fn key_equal_64(a : KeyValue, b : KeyValue) -> bool {
    return a.keyLow == b.keyLow && a.keyHigh == b.keyHigh;
}

@compute @workgroup_size(1)
fn sorted_merge(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    var previousIndex = 0u;
    var currentIndex = 0u;
    var outputIndex = 0u;
    while (previousIndex < params.counts.x || currentIndex < params.counts.y) {
        if (previousIndex >= params.counts.x) {
            mergeOutput[outputIndex] = mergeCurrent[currentIndex];
            mergeTags[outputIndex] = 1u;
            currentIndex += 1u;
        } else if (currentIndex >= params.counts.y) {
            mergeOutput[outputIndex] = mergePrevious[previousIndex];
            mergeTags[outputIndex] = 2u;
            previousIndex += 1u;
        } else {
            let previous = mergePrevious[previousIndex];
            let current = mergeCurrent[currentIndex];
            if (key_equal_64(previous, current)) {
                mergeOutput[outputIndex] = current;
                mergeTags[outputIndex] = 3u;
                previousIndex += 1u;
                currentIndex += 1u;
            } else if (key_less(previous, current)) {
                mergeOutput[outputIndex] = previous;
                mergeTags[outputIndex] = 2u;
                previousIndex += 1u;
            } else {
                mergeOutput[outputIndex] = current;
                mergeTags[outputIndex] = 1u;
                currentIndex += 1u;
            }
        }
        outputIndex += 1u;
    }
    operationResult[0] = outputIndex;
    operationResult[1] = 0u;
    operationResult[2] = 0u;
    operationResult[3] = 0u;
}

fn assign_scatter_impl(gid : vec3<u32>) {
    let index = gid.x;
    if (index >= params.counts.x) { return; }
    assignedIds[index] = 0xffffffffu;
    if (assignRequests[index] != 0u) {
        let rank = assignOffsets[index];
        if (rank < params.counts.y) { assignedIds[index] = assignFreeIds[rank]; }
    }
}

@compute @workgroup_size(64)
fn assign_scatter_64(@builtin(global_invocation_id) gid : vec3<u32>) {
    assign_scatter_impl(gid);
}
@compute @workgroup_size(128)
fn assign_scatter_128(@builtin(global_invocation_id) gid : vec3<u32>) {
    assign_scatter_impl(gid);
}
@compute @workgroup_size(256)
fn assign_scatter_256(@builtin(global_invocation_id) gid : vec3<u32>) {
    assign_scatter_impl(gid);
}

@compute @workgroup_size(1)
fn assign_finalize(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    var requested = 0u;
    if (params.counts.x != 0u) {
        let last = params.counts.x - 1u;
        requested = assignOffsets[last]
            + select(0u, 1u, assignRequests[last] != 0u);
    }
    let assigned = min(requested, params.counts.y);
    operationResult[0] = requested;
    operationResult[1] = assigned;
    operationResult[2] = requested - assigned;
    operationResult[3] = 0u;
}
