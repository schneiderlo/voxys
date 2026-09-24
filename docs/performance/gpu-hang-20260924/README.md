# Intel GPU hang investigation, 24 September 2026

The user reports that the game opens successfully but takes a long time to load.
They did **not** report a graphics-device-loss error. The resets below occurred
in the automated headless test browser; do not describe them as a reproduced
failure in the user's ordinary Chrome session.

## Environment

- Windows Chrome 153.0.8010.53, headless, Intel Iris Xe (`gen-12lp`).
- Installed Intel driver: `32.0.101.5768`, driver date 24 July 2024, as reported by
  `Win32_VideoController`. This age is context, not proof of the cause.
- Windows System event log contains Display event 4101: the Intel display driver
  stopped responding and recovered. These are real driver resets, not merely
  missed application loading deadlines.
- Staged startup candidate from `84593e9`, WASM SHA-256
  `c95e213ab332820602ead31fd93bba99373237f82b964d8a8f0f0507c5acea55`, with the
  previously preserved web assets. Concurrent UI edits are excluded.
- 1280 × 720 emulated viewport. Normal GPU timeout protection remains enabled.
- Runs are diagnostic, not performance benchmarks. Other CPU build activity
  was observed during this investigation; elapsed times are not speed claims.

## Controls

| Experiment | Result | What it establishes |
| --- | --- | --- |
| Timestamp queries disabled | `DXGI_ERROR_DEVICE_HUNG` at app frame 21 | GPU timing queries are not required to trigger the reset |
| CPU physics (`physicsBackend=jolt`) | Terrain initialization rejected this backend | Invalid control; no inference about GPU physics |
| Timestamp queries disabled, terrain raycasting dispatch omitted | Same reset at app frame 21 | The reset still occurs without executing `raycast_pipeline` |
| Timestamp queries disabled, each completed pass submitted separately | 303 frames, no reported GPU loss or errors | One successful diagnostic run; not proof of a fix |

Reports are stored as compressed JSON beside this file. Skipped dispatches are
explicitly labeled `SKIPPED` in the trace. Omitting rendering is only an
isolation experiment, not a proposed production workaround.

The no-timestamp trace records the loss callback with submission 79 queued and
76 completed. Submission 77 is the first full rendering command buffer: water
FFT, terrain raycasting, sky LUT, mesh shadows, scene lighting, meshes, water
composition, and primitive culling. It contains no physics simulation step.
The next two submissions were already queued by the time loss was reported;
this trace alone cannot identify the faulting instruction or fully exclude work
in those later submissions.

Some queue-completion promises settled after device loss. Their settlement is
not successful GPU execution. The initial no-timestamp archive retains those
late `end` fields; use the `lost` event's `completed: 76` snapshot, not the final
counter of 79. The revised tool explicitly records `settledAfterLoss` instead.

The corrected split-pass run submitted 5,938 command buffers and had observed
completion through submission 5,846 when its 303-frame sample was taken. The
world and controls rendered correctly in the inspected screenshot. Physics also
advanced. Its report is `split-passes.json.gz`.

Splitting changes submission scheduling and adds completion fences. It is a
lead for a smaller reproducer, not an established cause or production remedy:
an ordinary unsplit candidate also succeeded in the earlier startup report, so
this failure is intermittent. No specific shader or invalid command has been
identified. Timestamp queries and terrain raycasting are not individually
required for the observed failure. This investigation does not establish that
ordinary visible Chrome has the same failure as the automated test browser.

The user's slow launch remains a separate shader-compilation problem. The
diagnostic successes and failures occur after that compilation. No production
change, driver update, or deployment was made during this investigation.

## Local tooling

```sh
node scripts/performance/diagnose_gpu_hang.mjs \
  --chrome=PATH --site=STAGED_SITE --output=DIR \
  --profiling=0 --width=1280 --height=720
```

- `--profiling=0|1`: toggle the application's existing GPU timestamp queries.
- `--skip=REGEX`: omit matching draw/dispatch operations, for isolation only.
- `--split=1`: separate completed passes into individual command buffers and
  submissions so each has a completion marker. This changes scheduling.
- `--query=PARAMETERS`: additional URL parameters for explicit controls.

All runs add queue-completion instrumentation, which can affect scheduling.
A successful instrumented run is not proof of an uninstrumented fix. The probe
requires at least 300 app frames, no device loss, and no reported GPU errors;
for a deliberately skipped workload that is only diagnostic progress, not a
claim of correct gameplay.

The first split-pass attempt encountered a diagnostic proxy recursion with the
page's per-encoder timestamp-compatibility wrapper before rendering. It is not a
GPU-hang result. The proxy was corrected and that interaction added to the local
regression checks:

```sh
node --test scripts/performance/test_gpu_hang_trace.mjs
```

Three checks pass: command ordering across split passes, explicit targeted
omission, and rejection of post-loss completion as successful execution. No
GitHub workflow, CI step, or production renderer setting was added or changed.
