# DATA-06 independent observation review request

This is a request, not a completed review. Root implemented the transport,
codec, GameSession integration and probes. DATA-06/G01 remain unchecked.

Read root `AGENTS.md`, `README.md`, `GAME_IMPLEMENTATION_TODO.md` Contracts A–D
and DATA-04–06, this directory's `design.md`, `wire-format.md` and
`stage-b1/README.md` / `summary.json`. Use the tested source hashes, not HEAD
alone: the worktree contains authorized changes since the G00 commit.

Inspect `src/game/expedition/session_events.*`, `game_session.*`,
`session_recovery.*`, their tests, and the application identity/diagnostic
integration in `src/app/application.cpp`. Both native build systems, strict
checks and actual JS-exception/Asyncify WASM pass all 122 shared cases. Read the
assertions/probe bodies and independently examine failure paths; counts are
not a substitute for review.

Review these concrete questions:

1. Can a consumer obtain a writer, mutate accepted state, retain unbounded
   producer memory or advance/acknowledge a cursor through a failed read?
   Check owner-only operation, borrowed lifetime, gaps and in-place restart.
2. Can secret token/intent/escrow data escape via payloads, baselines or the
   browser diagnostic JSON? Are public incarnation/epoch/request references
   sufficient for the current single-participant profile? Freshness is the
   root's responsibility; reuse of any old incarnation is forbidden.
3. Are actual Pending/terminal/state/closure transitions truthful and ordered,
   including immediate rejection, delayed cancellation, compensation, empty
   build revival, retirement and closure? Can retry spam evict domain facts?
4. Can notification overwrite, invalid input or sequence exhaustion fail an
   accepted economic transaction, wrap an identity or silently lose a record?
   Verify sticky loss and exact out-of-band saturating counts.
5. Does baseline copy state and all cursors at one non-reentrant owner boundary,
   with checked bounded accounting and no partial caller publication on failure?
   Check ordered receipts and successful resumption after both ring overrun and
   receipt eviction without repeating a payment.
6. Does creation/recovery failure preserve reusable input, avoid backend calls
   and expose no partially initialized hub? Does read-only stored-state
   validation avoid returning authority? Do recovered sessions start quietly
   with a fresh incarnation, without reviving old local tokens/leases/events?
7. Are explicit byte fixtures independent of native padding, and all malformed
   versions/kinds/IDs/flags/optional fields/codes/NaN/Inf/trailing bytes rejected?
   Verify 64-bit JavaScript fields remain lossless and physical tick claims are
   reserved until actual completed-physics evidence exists.
8. Are publication/restart/read paths allocation-free under the real replacement
   allocator probes on native and configured WASM? Are old stage manifests
   clearly distinguished from the currently changed authority sources?

Reproduction commands and exact tool paths are in stage-b1. Use fresh output
directories; never overwrite historical evidence or suppress failures. Rebuild
all seven core sources for standalone probes. The current factory API requires
an explicit new `EventStreamIncarnation`; archived earlier-stage probe call
signatures are historical.

Write a separate report identifying reviewed source hashes/base, practical
findings with reproduction/reasoning, executed checks, fixes verified and
remaining limitations. State whether DATA-06's criteria are met. Do not claim
human visual review, durable save storage, real construction physics, network
replication or reserved-mechanic producers from these observation tests.

DATA-05 has a separate review request. G01 requires both tasks accepted plus its
headless canonical/inventory acceptance and the repository hook targets passing
before the authorized gate commit. LOOK-01/G02 remain visually unapproved.
