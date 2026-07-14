const BODY_ALIVE : u32 = 1u;
const BODY_AWAKE : u32 = 2u;
const BODY_STATIC : u32 = 4u;
const POSITION_ONE : i32 = 4096;
const UNIT_ONE : i32 = 1073741824;
const SECTOR_SIZE : i32 = 1048576;
const SECTOR_HALF : i32 = 524288;
const SENTINEL : u32 = 0xffffffffu;
const FNV_OFFSET : u32 = 2166136261u;
const FNV_PRIME : u32 = 16777619u;

struct LockstepBody {
    identity : vec4<u32>,
    sectorRadius : vec4<i32>,
    positionInvMass : vec4<i32>,
    linearVelocity : vec4<i32>,
};

struct LockstepContact {
    ids : vec4<u32>,
    normalPenetration : vec4<i32>,
};

struct LockstepParams {
    counts : vec4<u32>,
    integration : vec4<i32>,
    tick : vec4<u32>,
};

struct U64 {
    low : u32,
    high : u32,
};

@group(0) @binding(0) var<storage, read_write> bodies : array<LockstepBody>;
@group(0) @binding(1) var<storage, read_write> contacts : array<LockstepContact>;
@group(0) @binding(2) var<storage, read_write> roots : array<u32>;
@group(0) @binding(3) var<storage, read_write> bodyHashes : array<u32>;
@group(0) @binding(4) var<storage, read_write> contactHashes : array<u32>;
@group(0) @binding(5) var<storage, read_write> islandHashes : array<u32>;
@group(0) @binding(6) var<storage, read_write> telemetry : array<u32>;
@group(0) @binding(7) var<uniform> params : LockstepParams;

fn sat_add(a : i32, b : i32) -> i32 {
    let bits = bitcast<u32>(a) + bitcast<u32>(b);
    let result = bitcast<i32>(bits);
    if (a >= 0 && b >= 0 && result < 0) { return 2147483647; }
    if (a < 0 && b < 0 && result >= 0) { return -2147483647 - 1; }
    return result;
}

fn sat_sub(a : i32, b : i32) -> i32 {
    let bits = bitcast<u32>(a) - bitcast<u32>(b);
    let result = bitcast<i32>(bits);
    if (a >= 0 && b < 0 && result < 0) { return 2147483647; }
    if (a < 0 && b >= 0 && result >= 0) { return -2147483647 - 1; }
    return result;
}

fn signed_magnitude(value : i32) -> u32 {
    let bits = bitcast<u32>(value);
    return select(bits, 0u - bits, value < 0);
}

fn u64_add(a : U64, b : U64) -> U64 {
    let low = a.low + b.low;
    let carry = select(0u, 1u, low < a.low);
    return U64(low, a.high + b.high + carry);
}

fn u64_sub(a : U64, b : U64) -> U64 {
    let borrow = select(0u, 1u, a.low < b.low);
    return U64(a.low - b.low, a.high - b.high - borrow);
}

fn u64_less(a : U64, b : U64) -> bool {
    return a.high < b.high || (a.high == b.high && a.low < b.low);
}

fn u64_shift_left_two(value : U64) -> U64 {
    return U64(value.low << 2u,
               (value.high << 2u) | (value.low >> 30u));
}

fn multiply_u32(a : u32, b : u32) -> U64 {
    let aLow = a & 65535u;
    let aHigh = a >> 16u;
    let bLow = b & 65535u;
    let bHigh = b >> 16u;
    var result = U64(aLow * bLow, aHigh * bHigh);
    let first = aLow * bHigh;
    result = u64_add(result, U64(first << 16u, first >> 16u));
    let second = aHigh * bLow;
    result = u64_add(result, U64(second << 16u, second >> 16u));
    return result;
}

fn mul_shift(a : i32, b : i32, shift : u32) -> i32 {
    let negative = (a < 0) != (b < 0);
    var product = multiply_u32(signed_magnitude(a), signed_magnitude(b));
    product = u64_add(product, U64(1u << (shift - 1u), 0u));
    let value = (product.low >> shift) | (product.high << (32u - shift));
    let overflow = (product.high >> shift) != 0u;
    if (negative) {
        if (overflow || value > 2147483648u) { return -2147483647 - 1; }
        if (value == 2147483648u) { return -2147483647 - 1; }
        return -i32(value);
    }
    if (overflow || value > 2147483647u) { return 2147483647; }
    return i32(value);
}

fn integer_sqrt(input : U64) -> u32 {
    var value = input;
    var remainder = U64(0u, 0u);
    var root = 0u;
    for (var iteration = 0u; iteration < 32u; iteration += 1u) {
        root <<= 1u;
        let top = value.high >> 30u;
        remainder = u64_shift_left_two(remainder);
        remainder.low |= top;
        value = u64_shift_left_two(value);
        let candidate = U64((root << 1u) | 1u, root >> 31u);
        if (!u64_less(remainder, candidate)) {
            remainder = u64_sub(remainder, candidate);
            root += 1u;
        }
    }
    return root;
}

fn unit_fraction(numerator : u32, denominator : u32) -> u32 {
    if (denominator == 0u || numerator >= denominator) {
        return u32(UNIT_ONE);
    }
    var remainder = numerator;
    var quotient = 0u;
    for (var bit = 0u; bit < 30u; bit += 1u) {
        remainder <<= 1u;
        quotient <<= 1u;
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= 1u;
        }
    }
    return quotient;
}

fn hash_word(hash : u32, word : u32) -> u32 {
    return (hash ^ word) * FNV_PRIME;
}

fn body_alive(body : LockstepBody) -> bool {
    return (body.identity.z & BODY_ALIVE) != 0u;
}

fn body_dynamic(body : LockstepBody) -> bool {
    return body_alive(body) && (body.identity.z & BODY_STATIC) == 0u
        && body.positionInvMass.w > 0;
}

fn normalize_axis(bodyIndex : u32, axis : u32) {
    let local = bodies[bodyIndex].positionInvMass[axis];
    let biased = local + SECTOR_HALF;
    var sectorDelta = biased / SECTOR_SIZE;
    if (biased < 0 && biased % SECTOR_SIZE != 0) { sectorDelta -= 1; }
    bodies[bodyIndex].sectorRadius[axis] = sat_add(
        bodies[bodyIndex].sectorRadius[axis], sectorDelta);
    bodies[bodyIndex].positionInvMass[axis] = sat_sub(
        local, sectorDelta * SECTOR_SIZE);
}

fn make_contact(bodyA : u32, bodyB : u32,
                output : ptr<function, LockstepContact>) -> bool {
    let a = bodies[bodyA];
    let b = bodies[bodyB];
    if (!body_alive(a) || !body_alive(b)
        || (!body_dynamic(a) && !body_dynamic(b))) { return false; }
    var delta = vec3<i32>(0);
    var squared = U64(0u, 0u);
    for (var axis = 0u; axis < 3u; axis += 1u) {
        let sectorDelta = b.sectorRadius[axis] - a.sectorRadius[axis];
        if (abs(sectorDelta) > 1) { return false; }
        delta[axis] = sectorDelta * SECTOR_SIZE
            + b.positionInvMass[axis] - a.positionInvMass[axis];
        let component = signed_magnitude(delta[axis]);
        squared = u64_add(squared, multiply_u32(component, component));
    }
    let radiusSum = sat_add(a.sectorRadius.w, b.sectorRadius.w);
    if (radiusSum <= 0) { return false; }
    let radiusSquared = multiply_u32(u32(radiusSum), u32(radiusSum));
    if (!u64_less(squared, radiusSquared)) { return false; }
    let distance = integer_sqrt(squared);
    var normal = vec3<i32>(0);
    if (distance == 0u) {
        normal.x = select(-UNIT_ONE, UNIT_ONE, bodyA < bodyB);
    } else {
        for (var axis = 0u; axis < 3u; axis += 1u) {
            let fraction = unit_fraction(signed_magnitude(delta[axis]), distance);
            normal[axis] = select(i32(fraction), -i32(fraction), delta[axis] < 0);
        }
    }
    (*output).ids = vec4<u32>(bodyA, bodyB, 0u, 1u);
    (*output).normalPenetration = vec4<i32>(
        normal, sat_sub(radiusSum, i32(distance)));
    return true;
}

fn integrate_bodies() {
    for (var body = 0u; body < params.counts.x; body += 1u) {
        if (!body_dynamic(bodies[body])
            || (bodies[body].identity.z & BODY_AWAKE) == 0u) { continue; }
        bodies[body].linearVelocity.y = sat_add(
            bodies[body].linearVelocity.y, params.integration.x);
        for (var axis = 0u; axis < 3u; axis += 1u) {
            let delta = bodies[body].linearVelocity[axis]
                / params.integration.y;
            bodies[body].positionInvMass[axis] = sat_add(
                bodies[body].positionInvMass[axis], delta);
            normalize_axis(body, axis);
        }
    }
}

fn clear_contacts() {
    for (var contact = 0u; contact < params.counts.y; contact += 1u) {
        contacts[contact].ids = vec4<u32>(SENTINEL, SENTINEL, 0u, 0u);
        contacts[contact].normalPenetration = vec4<i32>(0);
    }
}

fn build_contacts(storedCount : ptr<function, u32>,
                  overflow : ptr<function, u32>) {
    clear_contacts();
    var stored = 0u;
    var total = 0u;
    for (var bodyA = 0u; bodyA < params.counts.x; bodyA += 1u) {
        if (!body_alive(bodies[bodyA])) { continue; }
        for (var bodyB = bodyA + 1u; bodyB < params.counts.x; bodyB += 1u) {
            var contact : LockstepContact;
            if (!make_contact(bodyA, bodyB, &contact)) { continue; }
            if (stored < params.counts.y) {
                contacts[stored] = contact;
                stored += 1u;
            }
            total += 1u;
        }
    }
    *storedCount = stored;
    *overflow = select(0u, 1u, total > params.counts.y);
}

fn build_islands(contactCount : u32) {
    for (var body = 0u; body < params.counts.x; body += 1u) {
        roots[body] = select(SENTINEL, body, body_alive(bodies[body]));
    }
    for (var round = 0u; round < 32u; round += 1u) {
        for (var index = 0u; index < contactCount; index += 1u) {
            let bodyA = contacts[index].ids.x;
            let bodyB = contacts[index].ids.y;
            let rootA = roots[bodyA];
            let rootB = roots[bodyB];
            let lower = min(rootA, rootB);
            let higher = max(rootA, rootB);
            if (higher < params.counts.x) { roots[higher] = lower; }
        }
        for (var body = 0u; body < params.counts.x; body += 1u) {
            let root = roots[body];
            if (root < params.counts.x) { roots[body] = roots[root]; }
        }
    }
}

fn solve_contacts(contactCount : u32) {
    for (var iteration = 0u; iteration < params.counts.w; iteration += 1u) {
        for (var index = 0u; index < contactCount; index += 1u) {
            let bodyA = contacts[index].ids.x;
            let bodyB = contacts[index].ids.y;
            var contact : LockstepContact;
            if (!make_contact(bodyA, bodyB, &contact)) { continue; }
            contacts[index] = contact;
            let dynamicA = body_dynamic(bodies[bodyA]);
            let dynamicB = body_dynamic(bodies[bodyB]);
            let correctionShift = select(30u, 31u, dynamicA && dynamicB);
            for (var axis = 0u; axis < 3u; axis += 1u) {
                let correction = mul_shift(
                    contact.normalPenetration.w,
                    contact.normalPenetration[axis], correctionShift);
                if (dynamicA) {
                    bodies[bodyA].positionInvMass[axis] = sat_sub(
                        bodies[bodyA].positionInvMass[axis], correction);
                    normalize_axis(bodyA, axis);
                }
                if (dynamicB) {
                    bodies[bodyB].positionInvMass[axis] = sat_add(
                        bodies[bodyB].positionInvMass[axis], correction);
                    normalize_axis(bodyB, axis);
                }
            }
            var normalVelocity = 0;
            for (var axis = 0u; axis < 3u; axis += 1u) {
                let relative = sat_sub(
                    bodies[bodyB].linearVelocity[axis],
                    bodies[bodyA].linearVelocity[axis]);
                normalVelocity = sat_add(normalVelocity, mul_shift(
                    relative, contact.normalPenetration[axis], 30u));
            }
            if (normalVelocity >= 0) { continue; }
            let closing = select(-normalVelocity, 2147483647,
                                 normalVelocity == -2147483647 - 1);
            let velocityShift = select(30u, 31u, dynamicA && dynamicB);
            for (var axis = 0u; axis < 3u; axis += 1u) {
                let impulse = mul_shift(
                    closing, contact.normalPenetration[axis], velocityShift);
                if (dynamicA) {
                    bodies[bodyA].linearVelocity[axis] = sat_sub(
                        bodies[bodyA].linearVelocity[axis], impulse);
                }
                if (dynamicB) {
                    bodies[bodyB].linearVelocity[axis] = sat_add(
                        bodies[bodyB].linearVelocity[axis], impulse);
                }
            }
        }
    }
}

fn hash_body(body : LockstepBody) -> u32 {
    var hash = FNV_OFFSET;
    hash = hash_word(hash, body.identity.x);
    hash = hash_word(hash, body.identity.y);
    hash = hash_word(hash, body.identity.z);
    hash = hash_word(hash, body.identity.w);
    hash = hash_word(hash, bitcast<u32>(body.sectorRadius.x));
    hash = hash_word(hash, bitcast<u32>(body.sectorRadius.y));
    hash = hash_word(hash, bitcast<u32>(body.sectorRadius.z));
    hash = hash_word(hash, bitcast<u32>(body.sectorRadius.w));
    hash = hash_word(hash, bitcast<u32>(body.positionInvMass.x));
    hash = hash_word(hash, bitcast<u32>(body.positionInvMass.y));
    hash = hash_word(hash, bitcast<u32>(body.positionInvMass.z));
    hash = hash_word(hash, bitcast<u32>(body.positionInvMass.w));
    hash = hash_word(hash, bitcast<u32>(body.linearVelocity.x));
    hash = hash_word(hash, bitcast<u32>(body.linearVelocity.y));
    hash = hash_word(hash, bitcast<u32>(body.linearVelocity.z));
    return hash_word(hash, bitcast<u32>(body.linearVelocity.w));
}

fn hash_contact(contact : LockstepContact) -> u32 {
    var hash = FNV_OFFSET;
    hash = hash_word(hash, contact.ids.x);
    hash = hash_word(hash, contact.ids.y);
    hash = hash_word(hash, contact.ids.z);
    hash = hash_word(hash, contact.ids.w);
    hash = hash_word(hash, bitcast<u32>(contact.normalPenetration.x));
    hash = hash_word(hash, bitcast<u32>(contact.normalPenetration.y));
    hash = hash_word(hash, bitcast<u32>(contact.normalPenetration.z));
    return hash_word(hash, bitcast<u32>(contact.normalPenetration.w));
}

fn hash_state(contactCount : u32) {
    var bodyAggregate = FNV_OFFSET;
    for (var body = 0u; body < params.counts.x; body += 1u) {
        let hash = hash_body(bodies[body]);
        bodyHashes[body] = hash;
        bodyAggregate = hash_word(bodyAggregate, hash);
        islandHashes[body] = 0u;
    }
    var contactAggregate = FNV_OFFSET;
    for (var index = 0u; index < params.counts.y; index += 1u) {
        var hash = 0u;
        if (index < contactCount) {
            hash = hash_contact(contacts[index]);
            contactAggregate = hash_word(contactAggregate, hash);
        }
        contactHashes[index] = hash;
    }
    var islandAggregate = FNV_OFFSET;
    for (var root = 0u; root < params.counts.x; root += 1u) {
        if (roots[root] != root || !body_alive(bodies[root])) { continue; }
        var hash = hash_word(FNV_OFFSET, root);
        for (var body = 0u; body < params.counts.x; body += 1u) {
            if (roots[body] == root) {
                hash = hash_word(hash, bodyHashes[body]);
            }
        }
        islandHashes[root] = hash;
        islandAggregate = hash_word(islandAggregate, hash);
    }
    var world = hash_word(FNV_OFFSET, params.tick.x);
    world = hash_word(world, bodyAggregate);
    world = hash_word(world, contactAggregate);
    world = hash_word(world, islandAggregate);
    telemetry[4] = world;
    telemetry[5] = bodyAggregate;
    telemetry[6] = contactAggregate;
    telemetry[7] = islandAggregate;
}

@compute @workgroup_size(1)
fn step_lockstep(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    var liveBodies = 0u;
    for (var body = 0u; body < params.counts.x; body += 1u) {
        liveBodies += select(0u, 1u, body_alive(bodies[body]));
    }
    var contactCount = 0u;
    var overflow = 0u;
    for (var substep = 0u; substep < params.counts.z; substep += 1u) {
        integrate_bodies();
        build_contacts(&contactCount, &overflow);
        build_islands(contactCount);
        solve_contacts(contactCount);
    }
    build_contacts(&contactCount, &overflow);
    build_islands(contactCount);
    telemetry[0] = liveBodies;
    telemetry[1] = contactCount;
    telemetry[2] = overflow;
    telemetry[3] = params.tick.x;
    hash_state(contactCount);
}
