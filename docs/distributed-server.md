# Distributed authoritative server slice

The world coordinator stores workers, island authority, checkpoints, swept
boundary proxies, migrations, retained rollback copies, and fault telemetry in
canonical ID order.

Swept proxies are exchanged for a fixed future horizon. Overlapping proxies on
different workers form deterministic connected components. The coordinator
chooses one destination using integer projected-load ratios and worker-ID tie
breaks. Every non-destination island restores the same checkpoint, shadow-runs,
and must match the source hash before the epoch switches. A mismatch leaves the
source authoritative.

Worker loss marks in-flight handoffs aborted, selects one surviving worker with
enough capacity, restores every affected island from its retained checkpoint,
and advances each epoch. Group recovery is conservative: all islands from the
lost worker move together, so an existing connected component cannot be split
during failover.

The native GPU backend uses wgpu-native/Vulkan on the current AMD target. It
uses the exact same schema, fixed-point constants, WGSL kernel, topology,
solver, and hashes as browser WebGPU. Multiple world/island dispatches are
encoded into one command encoder and retire with one queue submission. CUDA is
not selected on this non-NVIDIA target.

The three-world differential fixture compares every body byte and all aggregate
hashes with the scalar CPU oracle. Fault tests cover a dropped proxy, corrupted
shadow hash, stale epoch, destination selection, committed migration, retained
rollback checkpoint, worker loss, and component recovery.

On the Radeon 890M, 16 tiny worlds in one submission measured 0.578 ms p50;
16 isolated submissions measured 3.885 ms p50. See
[`benchmarks/phase11_native_server_batch_16.json`](benchmarks/phase11_native_server_batch_16.json).

```bash
nix-shell --run 'bazel test //tests:distributed_server \
  --runs_per_test=2 --test_output=errors'
nix-shell --run 'bazel test //tests:native_server_batch_benchmark \
  -c fastbuild --test_output=streamed --test_timeout=300'
```
