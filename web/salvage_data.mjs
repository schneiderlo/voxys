// Canonical construction foundation exchange, schema 1. No game state authority.
// JSON u64 fields are canonical decimal strings; arithmetic uses BigInt only.
export const MAX_U64 = (1n << 64n) - 1n;
export const PLACEMENT_SCHEMA_VERSION = 1;
export const MAX_PLACEMENT_RECORDS = 65536;
export const PLACEMENT_HEADER_BYTES = 44;
export const PLACEMENT_RECORD_BYTES = 37;
const MAX_GRID = 2147483647;
const MIN_NORMAL_F32 = 2 ** -126;

export function u64FromDecimal(text) {
  if (typeof text !== "string" || !/^(0|[1-9][0-9]{0,19})$/.test(text)) {
    throw new TypeError("u64 must be a canonical decimal string");
  }
  const value = BigInt(text);
  if (value > MAX_U64) throw new RangeError("u64 overflow");
  return value;
}

export function u64ToDecimal(value) {
  if (typeof value !== "bigint") throw new TypeError("u64 arithmetic requires BigInt");
  if (value < 0n || value > MAX_U64) throw new RangeError("u64 overflow");
  return value.toString(10);
}

function namespaceValid(world) {
  return typeof world === "string" && /^[0-9a-f]{32}$/.test(world) && !/^0{32}$/.test(world);
}

function validateRecord(record) {
  if (!namespaceValid(record?.id?.world) || u64FromDecimal(record?.id?.counter) === 0n) {
    throw new RangeError("invalid durable ID");
  }
  const position = record?.placement?.translation;
  for (const axis of ["x", "y", "z"]) {
    const value = position?.[axis];
    if (!Number.isInteger(value) || Math.abs(value) > MAX_GRID || Object.is(value, -0)) {
      throw new RangeError("invalid integer lattice position");
    }
  }
  const rotation = record?.placement?.rotation;
  if (!Number.isInteger(rotation) || rotation < 0 || rotation >= 24 || Object.is(rotation, -0)) {
    throw new RangeError("invalid proper rotation ID");
  }
}

function compareIds(a, b) {
  if (a.world !== b.world) return a.world < b.world ? -1 : 1;
  const x = u64FromDecimal(a.counter);
  const y = u64FromDecimal(b.counter);
  return x < y ? -1 : x > y ? 1 : 0;
}

function validateCounters(envelope) {
  const counters = [envelope.tick, envelope.revision, envelope.epoch, envelope.sequence].map(u64FromDecimal);
  if (counters[2] === 0n || counters[3] === 0n) throw new RangeError("epoch and sequence must be nonzero");
  return counters;
}

// Returns a new buffer, sorting a copy. Input objects are never mutated.
// This envelope is not a BuildModel, a command, or a complete save file.
export function encodePlacements(envelope) {
  const counters = validateCounters(envelope);
  if (!Array.isArray(envelope.records)) throw new TypeError("records must be an array");
  if (envelope.records.length > MAX_PLACEMENT_RECORDS) throw new RangeError("record capacity");
  for (const record of envelope.records) validateRecord(record);
  const records = [...envelope.records].sort((a, b) => compareIds(a.id, b.id));
  for (let i = 1; i < records.length; ++i) {
    if (compareIds(records[i - 1].id, records[i].id) === 0) throw new RangeError("duplicate durable ID");
  }
  const bytes = new Uint8Array(PLACEMENT_HEADER_BYTES + records.length * PLACEMENT_RECORD_BYTES);
  const view = new DataView(bytes.buffer);
  bytes.set([0x53, 0x56, 0x43, 0x50]); // SVCP
  view.setUint32(4, PLACEMENT_SCHEMA_VERSION, true);
  counters.forEach((value, index) => view.setBigUint64(8 + index * 8, value, true));
  view.setUint32(40, records.length, true);
  let offset = PLACEMENT_HEADER_BYTES;
  for (const record of records) {
    for (let i = 0; i < 16; ++i) {
      bytes[offset + i] = Number.parseInt(record.id.world.slice(i * 2, i * 2 + 2), 16);
    }
    view.setBigUint64(offset + 16, u64FromDecimal(record.id.counter), true);
    for (const [index, axis] of ["x", "y", "z"].entries()) {
      view.setInt32(offset + 24 + index * 4, record.placement.translation[axis], true);
    }
    bytes[offset + 36] = record.placement.rotation;
    offset += PLACEMENT_RECORD_BYTES;
  }
  return bytes;
}

export function decodePlacements(bytes) {
  if (!(bytes instanceof Uint8Array)) throw new TypeError("expected Uint8Array");
  if (bytes.length < PLACEMENT_HEADER_BYTES || bytes[0] !== 0x53 || bytes[1] !== 0x56
      || bytes[2] !== 0x43 || bytes[3] !== 0x50) throw new RangeError("invalid placement header");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint32(4, true) !== PLACEMENT_SCHEMA_VERSION) throw new RangeError("unsupported schema");
  const count = view.getUint32(40, true);
  if (count > MAX_PLACEMENT_RECORDS) throw new RangeError("record capacity");
  if (bytes.length !== PLACEMENT_HEADER_BYTES + count * PLACEMENT_RECORD_BYTES) {
    throw new RangeError("invalid placement byte count");
  }
  const result = {
    tick: u64ToDecimal(view.getBigUint64(8, true)),
    revision: u64ToDecimal(view.getBigUint64(16, true)),
    epoch: u64ToDecimal(view.getBigUint64(24, true)),
    sequence: u64ToDecimal(view.getBigUint64(32, true)),
    records: [],
  };
  validateCounters(result);
  let offset = PLACEMENT_HEADER_BYTES;
  for (let i = 0; i < count; ++i) {
    let world = "";
    for (let j = 0; j < 16; ++j) world += bytes[offset + j].toString(16).padStart(2, "0");
    const record = {
      id: { world, counter: u64ToDecimal(view.getBigUint64(offset + 16, true)) },
      placement: {
        translation: {
          x: view.getInt32(offset + 24, true),
          y: view.getInt32(offset + 28, true),
          z: view.getInt32(offset + 32, true),
        },
        rotation: bytes[offset + 36],
      },
    };
    validateRecord(record);
    if (i > 0 && compareIds(result.records[i - 1].id, record.id) >= 0) {
      throw new RangeError("duplicate or noncanonical ID order");
    }
    result.records.push(record);
    offset += PLACEMENT_RECORD_BYTES;
  }
  return result;
}

function cleanFloat(value) {
  const rounded = Math.fround(value);
  return rounded === 0 || Math.abs(rounded) < MIN_NORMAL_F32 ? 0 : rounded;
}

export function canonicalQuaternion(x, y, z, w) {
  if (![x, y, z, w].every(Number.isFinite)) throw new RangeError("nonfinite quaternion");
  const scale = Math.max(Math.abs(x), Math.abs(y), Math.abs(z), Math.abs(w));
  if (scale === 0) throw new RangeError("zero quaternion");
  x /= scale; y /= scale; z /= scale; w /= scale;
  const length = Math.sqrt(x * x + y * y + z * z + w * w);
  let result = { x: cleanFloat(x / length), y: cleanFloat(y / length), z: cleanFloat(z / length), w: cleanFloat(w / length) };
  if ([result.w, result.x, result.y, result.z].find(value => value !== 0) < 0) {
    result = Object.fromEntries(Object.entries(result).map(([key, value]) => [key, cleanFloat(-value)]));
  }
  validateQuaternion(result);
  return result;
}

function validateQuaternion(quaternion) {
  const components = [quaternion?.w, quaternion?.x, quaternion?.y, quaternion?.z];
  for (const value of components) {
    if (!Number.isFinite(value) || Math.abs(value) > 1 || Object.is(value, -0)
        || Math.fround(value) !== value || (value !== 0 && Math.abs(value) < MIN_NORMAL_F32)) {
      throw new RangeError("noncanonical quaternion component");
    }
  }
  if (!(components.find(value => value !== 0) > 0)
      || Math.abs(components.reduce((sum, value) => sum + value * value, 0) - 1) > 2e-6) {
    throw new RangeError("noncanonical quaternion norm or sign");
  }
}

export function encodeQuaternion(quaternion) {
  validateQuaternion(quaternion);
  const bytes = new Uint8Array(16);
  const view = new DataView(bytes.buffer);
  ["x", "y", "z", "w"].forEach((axis, i) => view.setFloat32(i * 4, quaternion[axis], true));
  return bytes;
}

export function decodeQuaternion(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length !== 16) throw new RangeError("invalid quaternion byte count");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const result = Object.fromEntries(["x", "y", "z", "w"].map((axis, i) => [axis, view.getFloat32(i * 4, true)]));
  validateQuaternion(result);
  return result;
}
