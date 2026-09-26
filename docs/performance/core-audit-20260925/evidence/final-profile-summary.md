# Final profile attribution

Diagnostic final-runner CPU/heap/I/O captures. Captures overlapped baseline GPU suite and do not provide clean latency/throughput/FPS comparisons. No additional workloads were run for this analysis.

| Edit hotspot | Baseline sampled ms / inclusive share | Final sampled ms / inclusive share |
|---|---:|---:|
| `voxy::game::adventure::CreativeVillage::admit` | 1058ms / 31.39% | 120ms / 4.78% |
| `voxy::game::adventure::validateConstruction (partial-inline)` | 851ms / 25.25% | 741ms / 29.52% |
| `voxy::game::expedition::sweepCoveTerrainSphere` | 266ms / 7.89% | 275ms / 10.96% |
| `voxy::game::adventure::AdventureRuntime::json[abi:cxx11]` | 218ms / 6.47% | 259ms / 10.32% |
| `partFor (inline)` | 167ms / 4.96% | 55ms / 2.19% |

Sample totals: edit3370→2510ms; idle900→1341ms. These are diagnostic samples, not wall-time speedups. Raw exclusive and inclusive values are both retained in JSON; filtered engine `flat` values fold hidden library/inlined callees into their parents.

Remaining `partFor`: all55ms (2.19%) come from `validateDoorClearance`, specifically its unchanged accepted-solids loop at `construction_policy.cpp:167`. That entire two-lookup expression has124ms source-attributed inclusive time. The move check at line149 has no samples. This is not the new sorted lookup falling back.

Village admission falls from1058 to120 sampled milliseconds. Its new index constructor has28ms (sorting27ms); construction-site source attribution is30ms. The unchanged paving-vs-fixed-group loop has34ms. These source numbers overlap and should not be summed.

Allocation: 449,142,060 bytes (428.34MiB), 1,065,140 objects; delta+2,118,048bytes (+2.02MiB / +0.474%) and+11objects. Retained-at-dump bytes/objects are unchanged at43,788,332 /3,257; those include retained golden observations and are not peak RSS.

Final replay I/O: {'ioctl': 60, 'brk': 5}. GPU queries=60, file/network data calls=0. Full source reports, focused caller tree and marker-delimited I/O summary are under `final-profiles/`. Exact commands are in `final-profile-analysis-commands.txt`.
