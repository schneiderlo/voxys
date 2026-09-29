# Accepted CPU change review

No correctness defect found in the accepted CPU changes.

- HUD content is completed in `hudContent_` before its one publication.
  `fillFrontierHud` has one caller, `refreshHud`, and only edits that content.
  AdventureHudPath still compares complete content, updates its refresh time,
  and invalidates both HUD/navigation layout factories for visible changes.
  Resize and navigation invalidation remain in their existing paths.
- Deferred combat sight uses the same captured actor/player positions and the
  same read-only spatial query. Attack-state advance does not publish geometry.
  Windup, Strike, Recovery and stagger paths return before the attack-start
  condition; strike contact and event validation still perform authoritative
  reachability checks. The same sight condition is required before a new attack.
- GpuDrawInstance's constructor initializes all original fields, including
  both live body index/generation words. Its size/alignment checks remain.
  Original PendingDraw values/comparator, sorted GPU-record copy, batching and
  static/live offsets remain in production. The rejected direct-pack map and
  range-loop experiment are absent from the production source.
- The paint table is fully filled with the existing float transfer function
  before publication. Byte-to-channel extraction remains modulo256, and opaque
  alpha remains1. All256 native and original-O2 WASM values were checked in the
  prior paired oracles; the shared building conversion uses the same formula.
- Read-only HUD/player tick accessors add no stored fields. The application
  scalar forwarding and WASM export retain zero for a missing runtime.

Collision source is byte-identical to both HEAD and the original captured source,
SHA256 `abcc45c566541f2cd6d359a0bd3aca8fa3b85b7036874136a487ba127fd33b94`.
The seven current boundary tests match the root's successful test-source hash.
They cover full publication capacity, every matching candidate slot, tie order,
negative sectors, independent concurrent reads, oversized fail-closed public
arguments and overlapping sweep normal precedence. The oversized test explicitly
documents that public travel/radius bounds reject its >64-sector envelope before
the private candidate guard; it does not claim coverage of that private branch.

The focused native mesh rerun uses the immutable CPU-current executable and its
51 hash-verified project libraries. Its mesh/HUD/combat sources match production;
its archived collision library still contains the earlier bitset experiment.
That library is not a proof of the restored current collision binary, which is
checked independently by the original-source seven-boundary executable.
Final whole-game build/cache integration is outside this focused proof.
