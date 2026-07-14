# Physics determinism contract

Voxys has two deliberately different contracts.

`DeterministicFloat` keeps stable semantic ordering and fixed iteration counts,
but its hashes are certified only for one exact backend, adapter, driver,
compiler, shader, and capacity tuple. It does not promise cross-vendor float
identity. The machine-readable matrix is
[`deterministic-float-certification.json`](deterministic-float-certification.json).

`Lockstep` uses sector-relative integer state and fixed-point arithmetic. Signed
rounding, saturation, division, square root, topology order, solver iteration
counts, and FNV-1a word hashing are explicit. The scalar CPU oracle and portable
WGSL kernel must produce identical bodies, contacts, island roots, and body,
contact, island, and world hashes.

Replay files use schema version 1 and explicit little-endian fields. They carry
the arithmetic/backend identity, build and shader fingerprints, content hashes,
simulation constants, capacity profile, complete checkpoint arrays, ordered
commands, and periodic hashes. No compiler padding, pointer, or GPU handle is
serialized. A trailing checksum rejects corruption before allocation.

Commands use `(tick, type priority, authoritative sequence)` as the primary
order. Producer, body handle, and payload fields only break malformed duplicate
keys. Stale generations are consumed but cannot mutate state.

Run the contract locally with:

```bash
nix-shell --run 'bazel test //tests:determinism_ci --test_output=errors'
```

CI sets `VOXY_REQUIRE_WEBGPU=1`, so an unavailable WebGPU/Vulkan path fails
instead of silently skipping the CPU/WGSL comparison.
