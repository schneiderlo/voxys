# Village clearance stays consistent across admission and placement

New or moved player parts now use the same approach-clearance check as fresh
village admission. Previously, a nearby part could avoid physical overlap and
be accepted during play, then cause the market to disappear on the next load
because fresh admission reserved more walking room.

The shared check compares each admitted village group's bounds with player-part
solids expanded **0.75 m horizontally**, using the existing **0.01 m overlap
margin**. Unchanged saved parts retain their priority. This does not enlarge
the central town's blanket build exclusion or change authored village anchors.

The focused regression proves three distinct cases:

- **New part at 0.20 m:** a grounded foundation sits 0.20 m behind the market's
  group envelope. It passes the general construction geometry check and has no
  direct overlap. The live village placement check refuses it to preserve the
  approach space. The accepted state remains unchanged.
- **The same part in an older save:** fresh admission preserves that exact
  player-owned state and defers the conflicting market group. Validating the
  unchanged old state remains allowed. The new rule does not delete or move
  legacy construction.
- **New part at 0.90 m:** moving the proposal another 0.70 m away passes both
  geometry checks. Fresh admission, reconstructed from player solids without
  the retained-layout shortcut, preserves all eight village groups' availability
  and exact bounds. The player-owned state remains unchanged by admission.

**Nine focused CPU cases passed in 769 ms.** These include the new consistency
case, the earlier doorway margin case, actual production-player route checks on
the CPU fixture, saved-pose clearance, legacy identity priority, retained-group
stability, bounded publication and town/NPC/resource access.

The parent task's updated **WASM and native builds both exited 0**. The retained
WASM log reaches `Built target voxy_wasm`; the native Bazel log reaches
`Build completed successfully` for `//:voxy_native`, producing
`bazel-bin/voxy_native`. The WASM build retains seven double-promotion warnings
in `village_layout.cpp`; this is not a warning-free build claim.

[Results, source hashes and artifact hashes](results.json) identify the checked
`village_layout.cpp` and `test_adventure_village.cpp`. The complete test log/XML
and both build logs are retained, gzip-compressed, in `checks/`.

Reproduce the same focused cases from the repository root:

```sh
nix-shell --run 'bazel test //tests:adventure --test_arg=--gtest_filter=AdventureVillage.* --test_output=errors --jobs=4'
```

This archive step ran **no new tests or builds**. The regression uses the
constant-sample CPU terrain fixture and simulates fresh derived-geometry
admission. It does not perform a real save-file/process restart or repeat the
earlier [full-terrain traversal](../village-r01/README.md). It does not establish
native/browser controls, rendered appearance, GPU performance or a completed
gameplay gate.
