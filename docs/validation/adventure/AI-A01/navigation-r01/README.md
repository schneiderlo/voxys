# AI-A01: layered navigation foundation

This record verifies the bounded CPU navigation foundation. **AI-A01 and G-C remain open.** The graph/follower are not yet integrated into runtime NPCs or enemies. These engineering fixtures are not an ordinary player journey, populated-world performance test, or visual acceptance.

## Recorded results

| Check | Result | Recorded test time |
| --- | --- | --- |
| `AdventureNavigation` | 9/9 passed | 155 ms |
| `AdventureWalkable` | 9/9 passed | 1 ms |
| Existing spatial-query, player, and construction-policy regressions | 18/18 passed | 26 ms |
| Final focused native target | Build/test command exited 0 | Its final invocation reused the passing navigation test cache. |
| Full `voxy_wasm` target | Build command exited 0 | Compilation/link only; no browser execution. |

Times come from the retained test XML; they are suite totals, not frame-time or path-latency claims. The selected `adventure` regression target contained 18 cases, even though its filter also named `AdventureNavigation`; navigation's nine cases belong to the separate target. The final native check builds the focused library/test dependency tree, **not the full native application**. The successful WASM executable was not packaged here and no running preview changed. Its compiler warnings remain in the archived log.

[results.json](results.json) records source hashes, exact test cases/timestamps, log hashes, and archived byte counts. Each input is compressed without changing its contents. The parent task reported exit 0 for final execution sessions 67356 and 90278; the retained build logs independently end in successful target completion. Archiving performed no new build, test, GPU, or UI run.

## What the foundation proves

- The graph samples **0.5 m cells** in **4 m tiles**, with at most **eight walkable layers per column** and **64 tiles**. An 8 × 8 tile region covers at most **32 m × 32 m** and has at most 32,768 layer nodes. This is local navigation, not a graph covering the full landscape.
- `walkableFeet` returns distinct, physically clear supported feet. Ground beneath a bridge and its deck remain separate. Headroom, actual stair tops, inclusive height limits, duplicate primitive surfaces, and overflow are checked. The query scans at most 64 distinct support levels; incomplete output preserves the destination and cannot publish a partial set of layers. Navigation treats incomplete columns as unavailable and returns capacity refusal for affected requests.
- A path uses the real `AdventurePlayer` as its edge probe, at most 24 fixed ticks per edge. `NavigationFollower` advances the authoritative character by at most one fixed step per call. It does not maintain a second movement solver. Jumping, swimming, and falling are not walking-graph edges.
- Actual compiled catalog floors/piers form a raised bridge. A follower crosses the deck and separately walks beneath it without switching layers. Lower feet match real terrain/stud support plus skin, with the full character below the deck; upper feet stay on the deck. Three actual catalog stair modules provide 18 treads for ascent and descent.
- Adding a wall invalidates the touched path before the next movement tick. A replacement route physically walks around it. Removing the middle deck makes the crossing unavailable; restoring it restores actual traversal.
- Synchronization compares the exact sorted old/new solid packets and dirties only tiles overlapping changed solids, conservatively expanded for capsule and neighboring-edge clearance. A distant edit retains unaffected tile generations. A retained path can continue immediately after synchronization while unrelated dirty columns rebuild.
- Paths bind their navigation owner, configuration, query source/revision, terrain descriptor, and tile generations. Another navigation instance cannot consume a foreign path. Terrain replacement at the same query revision stops the old follower. The follower also requires its actor to use the same query object and water policy.

`advance` defaults to **16 columns and four combined endpoint checks/A* expansions**. Hard per-call limits are **64 columns and 32 combined checks/expansions**. `request` performs no synchronous actor probes; endpoint attachment is incremental. Search stops at **4,096 A* expansions**, and paths are bounded to **512 waypoints**. Invalid configuration preserves the old configuration; stale input and capacity exhaustion refuse instead of following incomplete geometry. These work limits do not establish a population scheduler or a measured frame budget.

## Corrections retained honestly

[The initial navigation run](checks/navigation-r01.log.gz) passed six of seven cases. Its ground-under-bridge test incorrectly expected constant foot height over LEGO studs. The corrected assertion compares every lower-layer sample with actual terrain/stud support and requires the whole character to remain below the bridge. The upper-layer assertion remains constant deck height. Later tests additionally cover terrain rebinding and actor query/water ownership.

[The first span-target invocation](checks/navigation-spans-r01.log.gz) failed before tests because its BUILD registration referred to nonexistent `PROJECT_DEFAULT_COPTS`. The corrected target registration and subsequent passing span tests are retained separately. Neither failed invocation is counted as passing evidence.

## Still required

Runtime NPC/enemy integration, bounded multi-actor request scheduling, population/residency policy, measured frame p95, actual main-world encounters, and ordinary native/browser play remain unverified by this record. Collision admission and save compatibility for newly installed encounters must preserve existing homes and player poses. Combat, discovery rewards, outpost progression, and player enjoyment belong to their own open tasks.
