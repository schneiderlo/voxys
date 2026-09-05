# Fresh-eyes review — 2026-09-05

The review followed the root `AGENTS.md`, using the existing Nix environment.
Starting commit: `da4d98e`.

## Falling objects and brief freezes

The application runs GPU physics at fixed ticks, normally 60 Hz. Before this
change, every rendered frame used the latest exact physics pose. Frames between
ticks therefore repeated the same position, even at very high rendering FPS.

Presentation now interpolates the last two GPU poses in the existing camera
rebase pass. History stays on the GPU. There is no CPU pose readback and no
additional compute dispatch. Physics equations, substeps, solver settings, and
authoritative state are unchanged.

The history is optional and enabled by the graphical application. Headless and
authoritative worlds retain the default without history allocations. Storage is
48 bytes per allocated body slot; each physics tick copies the reachable body
prefix. This adds one fixed tick of presentation latency, as expected for
previous/current interpolation. Quaternion blending follows the shortest arc.
New object generations, large teleports, and distant sector changes do not blend
with stale history. Adjacent-sector motion remains continuous.

This does **not** eliminate every long frame. A stationary-camera Chrome run with
10,000 falling bodies averaged over 200 FPS but still had roughly 76–78 ms
99th-percentile frame intervals. CPU submission timing is not a measurement of
physical display scan-out. The user's exact scene and platform were not confirmed.

### Browser performance evidence

Chrome 152, hardware AMD WebGPU, 960×540, headless, full existing scene settings.
Three repetitions per body count, with workload order reversed on alternate runs.
These are medians of run summaries, not pooled percentiles. No other GPU workload
ran during the repeated measurements.

| Bodies | Before FPS | After FPS | Change | Before/after p99 interval |
|---:|---:|---:|---:|---:|
| 100 | 1,407.23 | 1,468.21 | +4.3% | 5.1 / 4.8 ms |
| 1,000 | 898.68 | 918.43 | +2.2% | 8.6 / 8.6 ms |
| 5,000 | 371.09 | 367.88 | −0.9% | 23.5 / 26.3 ms |
| 10,000 | 213.89 | 212.28 | −0.8% | 77.7 / 75.9 ms |

Throughput stayed close in the heavy cases. This does not establish a universal
speedup or unchanged tail latency: the 10,000-body p95 increased from 25 to 30 ms,
and browser timings varied between repetitions. Rendering quality was not lowered.

The final artifact smoke run was slower: 832.24 / 190.22 FPS at 1,000 / 10,000
bodies. A subsequent saved-baseline control was also slower than the earlier
matrix, at 852.86 / 195.76 FPS. The final single comparison is therefore −2.4% /
−2.8%; neither version reproduced its earlier median. These records are retained
alongside the three-run matrix, rather than treating the smoke run as a speedup
or attributing all variation to the code. Broader hardware coverage is still needed.

The saved pre-interpolation WASM build already included earlier review fixes.
The later build also includes the GPU query fix and shutdown fix below. This is
an application comparison across those late changes, not a shader microbenchmark.

A four-frame queue experiment reduced the 10,000-body p99 to 45.4 ms but reduced
throughput to 152.28 FPS. It was discarded. The retained queue limit is eight.
Another experiment delayed the completion callback until four submissions were
outstanding while retaining the eight-frame limit. It reached 206.93 FPS and
80.7 ms p99 at 10,000 bodies, with worse results at intermediate counts. It was
also discarded; browser scheduling code is unchanged.

A diagnostic run sampled 10,000-body GPU physics at 9.17 ms median and 16.00 ms
p95 per sampled tick. Sampled rendering was 0.36 ms median. Its 216.55 FPS average
still accompanied a 74.3 ms p99 frame interval. This supports investigating
submission/completion bursts separately from the interpolation issue; it does
not establish a single shader as the cause of every visible pause.

Compact [measurement records](benchmarks/fresh-eyes-2026-09-05/) include individual
run metrics and artifact hashes. Full frame samples remain in the local
`/tmp/voxys-freeze-*.json` files.

## Other confirmed fixes

| Area | Failure corrected | Regression evidence |
|---|---|---|
| GPU events | Multiple encodes before one submission overwrote earlier tick parameters. An optional fallback binding was also smaller than the shader's runtime element. | One encoder containing ticks 41/42/43, full-ring rejection, and slot reuse. |
| GPU queries | Batched encodes could execute the last uploaded requests in earlier batches. | Different request counts and IDs in one encoder; slot reuse; device capacity rejection. |
| GPU shutdown | Unsubmitted initial texture uploads could refer to textures destroyed during shutdown. | Shut down before the first tick, then submit on the same queue. |
| Terrain files | Buffered writes could report success when closing the file failed. | `/dev/full` tests and CLI checks for both raw and LDH output. |
| Heightmaps | Resize retained an obsolete GPU texture/mip chain; moved-from objects retained cached extrema. | Real GPU resize/re-upload test and move-cache tests. |
| Authored cove | Off-map/extreme coordinates could overflow integer conversions or produce enormous allocation sizes. | Off-map, non-finite, extreme-coordinate, and tiny-cell tests under sanitizers. |
| VMESH loading | Empty optional sections reached null-pointer `memcpy`; corrupt offsets/counts could wrap bounds checks. | Real asset loading plus malformed string/animation records under sanitizers. |
| Race progression | Exact-plane and tiny checkpoint crossings were missed; lobby ticks consumed the countdown. | Exact/tiny crossings and countdown started after lobby time elapsed. |
| Native transport | Failing over an empty gateway dereferenced a null primary endpoint. | Empty-gateway regression under sanitizers. |
| Browser transport | Concurrent failures duplicated fallback connections; old asynchronous operations could affect a new connection or revive a closed one. Invalid sends could trigger fallback. | Ten Node tests covering framing, validation, concurrent fallback, close/reconnect races, and queued sends. |
| Ridgebreak encoding | The encoder accepted noncanonical unused terrain sample slots that its decoder rejected. | Encoder/decoder agreement regression. |
| Native Bazel build | The entry point included a generated content header absent from its dependency closure. | Added the existing content-header target as a direct dependency and rebuilt. |
| WRECKWATER startup | Native TCP requests explicit admission, but the runtime only handled already-connected events. All clients could wait at tick zero. | Failing-before/passing-after admission test; reconnect and rejection tests; full four-client native process proof. |

GPU event/query uploads now use storage associated with their outstanding
readback slot. Existing buffers remain pooled. A full ring rejects new work
before it overwrites parameters still in use.

The browser transport tests run in a small dedicated CI workflow. They use
controlled Web Streams and peer implementations; they do not claim a live
WebTransport/WebRTC server integration test.

The WRECKWATER fix accepts the exact proposed connection serial after committing
the authority generation, and stops the authority if transport admission fails.
It keeps explicit admission enabled. The local five-process proof passed with
four clients, reconnects, 10% injected realtime loss, duplicates, and reordering.
The existing proof script was copied to `/tmp` solely to add `-c opt` to its Bazel
build command. Proof behavior and checks were retained.

Four stale coastal test expectations were updated to the current authored
profile and shader constants. Production coastal shader constants were not
changed. The original baseline reproduced those four failures.

The cove's valid-output comparison retained identical hashes and statistics over
100 runs at each tested size. Median times were 25.36 → 25.23 ms at 256² and
25.16 → 25.07 ms at 8192². The inner generation loop was not changed.

## Validation commands

Final optimized combined suite: **1,431 passed, three skipped** across 215
suites; four tests remain disabled. The native executable builds successfully.
The full UBSan/float-cast-overflow run passed 1,428 tests with three skips before
the final capacity/admission additions; both additions then passed focused UBSan
targets. The final optimized suite includes all additions.

The interpolation GPU oracle was also run against the original shader in an
isolated directory. It failed on repeated positions and sector-boundary motion.
The same oracle passes with the delivered shader.

Run directly inside the existing Nix environment:

```bash
bazel test -c opt //tests:voxy_tests --test_output=errors
bazel test -c opt --config=ubsan --copt=-fsanitize=float-cast-overflow \
  --linkopt=-fsanitize=float-cast-overflow \
  --test_env=UBSAN_OPTIONS=halt_on_error=1 //tests:voxy_tests --test_output=errors
node --test scripts/test_network_transport.mjs
bazel build --config=wasm //:voxy_wasm
node scripts/benchmark_browser.mjs --target local \
  --artifact-dir bazel-bin/voxy_wasm --quick --runs 3 --headless \
  --resolution 960x540 --output /tmp/voxys-browser-review.json
```

AddressSanitizer checks also covered compression, heightmaps, VMESH, race,
networking, GPU events, and GPU queries. Terrain/shadow golden outputs remained
bit-exact. The window and two opt-in GPU benchmarks are expected skips; four
other tests remain disabled by the project.
