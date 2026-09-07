import assert from "node:assert/strict";
import test from "node:test";
import {
  MAX_U64, MAX_PLACEMENT_RECORDS, u64FromDecimal, u64ToDecimal,
  encodePlacements, decodePlacements, canonicalQuaternion, encodeQuaternion, decodeQuaternion,
} from "../web/salvage_data.mjs";

const WORLD = "000102030405060708090a0b0c0d0e0f";
const GOLDEN = Buffer.from(
  "5356435001000000010000000000200007000000000000000200000000000000"
  + "ffffffffffffffff02000000"
  + "000102030405060708090a0b0c0d0e0f0001000000000000ceffffff100000000900000001"
  + "000102030405060708090a0b0c0d0e0fffffffffffffffff32000000d0ffffff0000000002", "hex");

function goldenEnvelope() {
  return {
    tick: "9007199254740993", revision: "7", epoch: "2", sequence: "18446744073709551615",
    records: [
      { id: { world: WORLD, counter: "256" }, placement: { translation: { x: -50, y: 16, z: 9 }, rotation: 1 } },
      { id: { world: WORLD, counter: "18446744073709551615" }, placement: { translation: { x: 50, y: -48, z: 0 }, rotation: 2 } },
    ],
  };
}

test("u64 JSON strings preserve values beyond Number precision and reject ambiguous syntax", () => {
  for (const value of [0n, 1n, (1n << 53n) - 1n, 1n << 53n, (1n << 53n) + 1n, MAX_U64]) {
    const json = JSON.stringify({ value: u64ToDecimal(value) });
    assert.equal(u64FromDecimal(JSON.parse(json).value), value);
  }
  for (const value of [0, 1, 9007199254740993, null, undefined, "", "00", "01", "-0", "-1", "+1", " 1", "1 ", "1.0", "1e2", "18446744073709551616"]) {
    assert.throws(() => u64FromDecimal(value));
  }
  for (const value of [0, "1", -1n, MAX_U64 + 1n]) assert.throws(() => u64ToDecimal(value));
});

test("C++ frozen bytes decode and encode exactly, preserving insertion independence", () => {
  assert.deepEqual(decodePlacements(GOLDEN), goldenEnvelope());
  assert.deepEqual(Buffer.from(encodePlacements(goldenEnvelope())), GOLDEN);
  const reversed = goldenEnvelope();
  reversed.records.reverse();
  const original = structuredClone(reversed);
  assert.deepEqual(Buffer.from(encodePlacements(reversed)), GOLDEN);
  assert.deepEqual(reversed, original);
  assert.deepEqual(JSON.parse(JSON.stringify(decodePlacements(GOLDEN))), goldenEnvelope());
  // Nonzero buffer offsets must not shift the parser's input frame.
  const padded = Buffer.concat([Buffer.alloc(7), GOLDEN, Buffer.alloc(11)]);
  assert.deepEqual(decodePlacements(padded.subarray(7, 7 + GOLDEN.length)), goldenEnvelope());
});

test("ID order uses namespace then numeric counter rather than wire bytes", () => {
  const envelope = goldenEnvelope();
  envelope.records[0].id.counter = "256";
  envelope.records[1].id.counter = "1";
  assert.equal(decodePlacements(encodePlacements(envelope)).records[0].id.counter, "1");
  envelope.records[1].id.world = "100102030405060708090a0b0c0d0e0f";
  assert.equal(decodePlacements(encodePlacements(envelope)).records[0].id.counter, "256");
});

test("strict decoder rejects truncations, trailing data, schema changes and corrupt records", () => {
  for (let length = 0; length < GOLDEN.length; ++length) {
    assert.throws(() => decodePlacements(GOLDEN.subarray(0, length)));
  }
  assert.throws(() => decodePlacements(Buffer.concat([GOLDEN, Buffer.alloc(1)])));
  for (const [offset, value] of [[0, 0], [4, 2], [24, 0], [80, 24]]) {
    const bytes = Buffer.from(GOLDEN);
    bytes[offset] = value;
    assert.throws(() => decodePlacements(bytes));
  }
  let bytes = Buffer.from(GOLDEN);
  bytes.writeUInt32LE(0xffffffff, 40);
  assert.throws(() => decodePlacements(bytes));
  bytes = Buffer.from(GOLDEN);
  bytes.writeInt32LE(-2147483648, 68);
  assert.throws(() => decodePlacements(bytes));
  bytes = Buffer.from(GOLDEN);
  bytes.copy(bytes, 81, 44, 68);
  assert.throws(() => decodePlacements(bytes));
  bytes = Buffer.concat([GOLDEN.subarray(0, 44), GOLDEN.subarray(81), GOLDEN.subarray(44, 81)]);
  assert.throws(() => decodePlacements(bytes));
});

test("encoder rejects invalid IDs, coordinates, typed counters and capacity overflow", () => {
  const mutations = [
    e => { e.tick = 1; }, e => { e.epoch = "0"; }, e => { e.sequence = "0"; },
    e => { e.records[0].id.counter = "0"; },
    e => { e.records[0].id.world = "0".repeat(32); },
    e => { e.records[0].id.world = WORLD.toUpperCase(); },
    e => { e.records[0].placement.translation.x = -2147483648; },
    e => { e.records[0].placement.translation.x = 0.5; },
    e => { e.records[0].placement.translation.x = NaN; },
    e => { e.records[0].placement.translation.x = -0; },
    e => { e.records[0].placement.rotation = 24; },
    e => { e.records.push(e.records[0]); },
    e => { e.records.length = MAX_PLACEMENT_RECORDS + 1; },
  ];
  for (const mutate of mutations) {
    const envelope = goldenEnvelope();
    mutate(envelope);
    assert.throws(() => encodePlacements(envelope));
  }
  const empty = { tick: "0", revision: "0", epoch: "1", sequence: "1", records: [] };
  assert.deepEqual(decodePlacements(encodePlacements(empty)), empty);
});

test("quaternion normalization uses canonical sign, xyzw wire order and stable decoded bits", () => {
  assert.deepEqual(canonicalQuaternion(-2, -0, -0, -0), { x: 1, y: 0, z: 0, w: 0 });
  assert.deepEqual(canonicalQuaternion(1, 2, 3, 4), canonicalQuaternion(-1, -2, -3, -4));
  assert.deepEqual(Buffer.from(encodeQuaternion({ x: 1, y: 0, z: 0, w: 0 })),
    Buffer.from("0000803f000000000000000000000000", "hex"));
  assert.deepEqual(Buffer.from(encodeQuaternion(canonicalQuaternion(1, 0, 0, 1))),
    Buffer.from("f304353f0000000000000000f304353f", "hex"));
  const expected = encodeQuaternion(canonicalQuaternion(1, 2, 3, 4));
  let actual = expected;
  for (let i = 0; i < 100; ++i) actual = encodeQuaternion(decodeQuaternion(actual));
  assert.deepEqual(actual, expected);
  for (const q of [[0, 0, 0, 0], [0, 0, 0, Infinity], [NaN, 0, 0, 1]]) {
    assert.throws(() => canonicalQuaternion(...q));
  }
  for (const q of [{ x: -0, y: 0, z: 0, w: 1 }, { x: 0, y: 0, z: 0, w: -1 },
    { x: 2 ** -149, y: 0, z: 0, w: 1 }, { x: 0, y: 0, z: 0, w: 0.5 }]) {
    assert.throws(() => encodeQuaternion(q));
  }
  for (let length = 0; length < 16; ++length) assert.throws(() => decodeQuaternion(new Uint8Array(length)));
  for (const hex of ["0000008000000000000000000000803f", "000000000000000000000000000080bf", "0000c07f00000000000000000000803f"]) {
    assert.throws(() => decodeQuaternion(Buffer.from(hex, "hex")));
  }
});
