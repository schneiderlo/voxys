# Fresh baseline profile attribution

Fresh native optimized creative CPU replay, 768 parts. Diagnostic profiles overlapped the baseline GPU suite; no latency, throughput or FPS claim. CPU uses 36000 updates, heap and I/O 3600.

## CPU hotspots

Unfiltered exclusive / inclusive percentages; inclusive percentages overlap.

| Workload | Function | Exclusive | Inclusive |
|---|---|---:|---:|
| edit | `voxy::game::adventure::CreativeVillage::admit` | 0.30% | 31.39% |
| edit | `voxy::game::adventure::validateConstruction (partial-inline)` | 6.26% | 25.25% |
| edit | `voxy::game::expedition::sweepCoveTerrainSphere` | 1.81% | 7.89% |
| edit | `voxy::game::adventure::AdventureRuntime::json[abi:cxx11]` | 0.03% | 6.47% |
| edit | `partFor (inline)` | 4.93% | 4.96% |
| idle | `voxy::game::adventure::AdventureRuntime::json[abi:cxx11]` | 0.00% | 22.00% |
| idle | `voxy::game::expedition::sweepCoveTerrainSphere (partial-inline)` | 4.89% | 15.22% |
| idle | `voxy::game::adventure::AdventureRuntime::refreshHud` | 0.22% | 10.33% |
| idle | `voxy::game::adventure::AdventureRuntime::updateTarget` | 3.22% | 6.56% |
| idle | `voxy::game::expedition::CoveInputRouter::tick (partial-inline)` | 5.33% | 5.44% |

The village reservation loop alone accounts for 969 of 3,370 sampled milliseconds (**28.75%**); partFor accounts for 167 (**4.96%**). These are source attributions, not independent speedup promises.

## Allocation and I/O

Allocation replay: 426.32 MiB in 1,065,129 objects. Top byte consumers: JSON 26.38%, scenery 20.11%, construction validation 14.52%, Blacksmith append 11.94%, geometry compiler 11.30%. Validation owns 61.87% of allocation objects including callees.

Marker-delimited replay I/O: {'ioctl': 60, 'brk': 6}; all 60 ioctls are DRM sync-object queries. No file/network read/write/sync calls occurred. Startup and golden writes are outside the interval.

## Interpretation limits

- CPU exclusive percentages use UNFILTERED reports. -show=voxy:: folds filtered frames into visible parents, so its flat column is engine-attributed rather than raw self samples.
- Inclusive percentages overlap (village is inside scenery; partFor is inside validation); never add them.
- Benchmark validity-string search consumes 28.78% inclusive idle CPU; it is harness work, not a game optimization opportunity.
- Allocation percentages are volume rather than retained memory or CPU time; filtered reports fold allocator frames into engine callers.
- No startup/durable save/network workload was measured in the replay I/O interval.

Exact reproduction/analysis commands: `profile-analysis-commands.txt`; parsing script: `analyze_profiles.py`; full machine record: `profile-summary.json`.
