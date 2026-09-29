# Prepared direct GPU record packing

The isolated native binaries and original-O2 WASM assembly have been built.
The paired native record/work/image oracle passed: each variant ran30 of31
declared tests, with one existing disabled test and zero failures or skips.
All384 record/work files and358 image capture files match exactly across181
recorded frames and542 expanded records. Required coverage is asserted in
correctness-result.json.
Production source and flags are unchanged by this prototype.

Decision: do not promote the mapping prototype. Six fresh balanced native trials
per variant produced large variation and a regression in the 600-live case.
The median-of-trial-medians changes were +8.6% for 600 live forest instances,
-22.3% for 6000 live forest instances, and -10.8% for the retained mixed scene.
The three balanced block changes were +15.0/-38.1/+110.7%,
-5.9/-44.8/-11.4%, and -18.7/-21.0/+8.6%, respectively. The normal-size routes
change direction across blocks. All raw trial medians, p95 values and unchanged
work counts remain in component-result.json. Scratch capacity fell about 38.2%.
This is a native command-encoding component test with discarded raster commands;
it provides no displayed FPS result.

`baseline-mesh_path.cpp` captures the current CPU candidate: in-place GPU
construction, retained frame scratch, and exact byte paint LUT.
`candidate-mesh_path.cpp` retains one `size_t` source index per visible expanded
draw, sorts the identical original PendingDraw array, then constructs each final
128-byte GPU record directly from its source. The GPU vector reserves the exact
visible expanded count once after sorting; retained warm capacity remains. It
removes unnecessary initial source-count reservation and sortedInstances and
accounts for the compact map in frameScratchCapacityBytes.

The PendingDraw definition, initial draw values, comparator, batching predicate,
validation order, culling, expanded capacity checks, static record behavior,
dynamic offsets, live body index/generation and final uploads remain the same.
The mapping count equals the old first GPU vector count at every expansion.
Both use the existing uint32 bound for expanded draws. The original source index
uses size_t: addInstance imposes no source-count cap, and arbitrarily many culled
source entries must not introduce a new uint32 truncation constraint.

Source references remain valid through packing: render does not mutate instances_
or invoke the scene callback before packing completes. Concurrent mutation was
already unsupported by the original validation/collection traversal.

`direct-index-mesh_path.cpp` is an optional alternative that widens the private
PendingDraw field to size_t and avoids the separate map. The primary candidate
keeps the original draw layout and sort inputs exactly, including on native64.

`range-ordinal-mesh_path.cpp` is an optional source-only follow-up. It replaces
the indexed validation traversal with the original range traversal plus a size_t
ordinal consumed before every branch. This may avoid repeated wasm32 source-count
division while preserving original source indices for culled entries. It has not
been compiled, checked against the oracle, or measured. All other mapping code
is identical; the measured mapping source and binary hashes remain unchanged.

Root-controlled verification:

1. Run prepare-recording.py, then build.py. Both instrumented sources use the
   identical previously reviewed record capture block and matching frozen test
   object, libraries, shaders and native O3 compiler options.
2. Run run-oracle.py. Require all active actual-render tests, every final GPU
   record byte, retained record/selection byte, normalized draw field, upload,
   culling/submesh counter and RGBA frame to match. Scratch capacity is expected
   to change; neither logical capacity nor uploads should change.
3. Run build-wasm-assembly.py with frozen original scalar O2 arguments. Inspect
   the final sorted packing path and constructor append helper: it should write
   one GPU destination and perform no128-byte sorted record copy. Vector growth
   copies may still exist and must not be mistaken for hot sorted-pack traffic.
4. Build uninstrumented native objects before any clean balanced component
   timing. The existing disabled ForestCpuEncodingBenchmark calls the actual
   renderer for600/6000 live forest inputs and a retained village/character
   workload, drains uploads outside the interval and discards raster commands.
   It is a CPU component measurement, not GPU timing or displayed FPS.

Source-level destination writes per expanded draw go from256 GPU-record bytes
to128 GPU-record bytes plus sizeof(size_t) map bytes (4 wasm32,8 native64).
Map reads replace a128-byte sorted-source read. These arithmetic traffic counts
exclude relocation and unchanged work. Actual original-O2 wasm assembly confirms
the baseline sorted append performs memory.copy of128 bytes, while the candidate
direct constructor stores the final record fields once and has no warm-path
memory.copy. GPU uploads and render stack size remain unchanged. See
lowering-result.json. Reduced work did not establish a consistent improvement
for the normal-size component fixtures, so the prototype remains unpromoted.
