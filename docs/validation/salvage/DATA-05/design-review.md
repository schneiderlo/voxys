# DATA-05 independent design review

Reviewed by `simulation_production`, 2026-09-07, before implementation.
The five corrections below were approved by root and incorporated into
[design.md](design.md). No implementation or passing result is claimed.

1. **Ordered cancellation retains decision capacity.** Canceling request two
   releases adapter/debit/candidate reservations once, but retains its queue
   slot and terminal receipt/journal reservation until request one terminates.
   Otherwise a full outbox could prevent the processed frontier from closing.
2. **Reserve all record kinds, sequence only on append.** Account for admission,
   terminal, allocator lease and closure records together. Assign journal IDs in
   actual append order and reserve remaining sequence headroom. A terminal ID
   assigned at admission would precede an intervening second admission on paper
   while being appended afterward. A separate closure reservation keeps Leave
   available when normal admission is full.
3. **Replay accepted deltas before clearing history.** Recovery validates exact
   pre-state and contiguous marker coverage, applies accepted object/account
   transitions once, then clears history and retires the old admission. It does
   not invoke Undo after its private escrow has been discarded, nor execute
   commands a second time.
4. **Dormant builds retain revision high-water.** Build removal/restoration each
   advances the retained topology revision and session revision. Read-only
   history summaries expose current expected target/entry/generation/direction
   so a caller can author Undo/Redo without receiving mutable escrow data.
5. **Eviction respects surviving references.** Retire a dormant logical object
   only when the bounded surviving history has no reference to it. For example,
   evicting the old Add entry must not destroy the same part retained by a newer
   Remove entry. No unbounded history graph or ID reuse is introduced.

The existing purchase/dismantle inverse equations are sound; both currencies
must be checked independently. The single-participant, volatile memory journal
and fake preparation boundaries are explicit. Native/browser disk durability,
real GPU publication and multi-participant history remain later gates.
