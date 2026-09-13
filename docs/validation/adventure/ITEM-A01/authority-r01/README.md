# Adventure item and home authority — first implementation

Baseline: `acd9d43d224734a633c3af5db5d6d519e6bc3e23`. This record covers the new shared CPU implementation. It does not certify the integrated home journey or complete G-A. The containing implementation commit supplies the final source revision.

## Player rules

- A new adventure starts with **640 wood, 320 stone and 80 scrap**, granted once. Restore retains the saved quantities and refuses a missing starter-grant marker.
- The backpack has 24 slots; each chest has 32. Wood, stone and scrap stack to 999. A field hammer occupies one slot and can be equipped separately.
- Building consumes the installed piece cost. Removing a pristine piece refunds that same cost, provided the backpack can hold every refunded material. An unusable preview, cancelled preparation or failed publication spends nothing.
- Chest construction creates its stable functional component in the same transaction as the part. A nonempty chest cannot be removed. Item transfers accept a source slot and quantity, check both container revisions and update both sides together.
- At a reachable workbench, 4 wood and 2 scrap craft a field hammer. No output space means no ingredients are lost. Equipping the hammer doubles the yield of subsequently gathered installed resource nodes.
- Each installed resource node can be gathered once. Depletion survives restore; it is not tied to visual chunk residency.
- A usable bed registers recovery and restores health. Runtime geometry must establish shelter, reachable furniture and safe space. Removing the registered bed switches recovery to the installed town fallback.

The runtime uses the shared candidate policy for support, terrain, overlap, player reach and protected access. An inventory-only unit-test validator is explicitly a test seam, not an alternate shipping path.

## Authority boundaries

Sources:

- [Item catalog and bounded stack operations](../../../../../src/game/adventure/item_catalog.hpp)
- [Session records and prepared commands](../../../../../src/game/adventure/adventure_session.hpp)
- [Session implementation](../../../../../src/game/adventure/adventure_session.cpp)
- [Focused tests](../../../../../tests/test_adventure_session.cpp)

World namespaces use the existing 16-byte construction identity. Structure, part and component counters share one checked high-water mark. Counters 1 and 2 reserve the solo owner and backpack; subsequent IDs are never reused after removal. Canonical arrays have increasing IDs, and all functional references must resolve exactly once.

Limits are four structures, 1,024 total accepted parts and 32 components. Part positions use the existing .02 m lattice and one of four proper yaw rotations. Structure origins establish region ownership; part positions are absolute world coordinates. Player pose remains continuous and finite.

Each command supplies the expected accepted revision, monotonic request sequence and owner. A request at or below the persisted request high-water mark refuses. Commands build a private complete candidate; runtime can prepare collision/render data from its immutable view. Only the owning session may commit that candidate, and any intervening movement or command makes it stale. Thus a delayed edit cannot overwrite newer inventory or player state.

Removal and undo are compensating transactions, not whole-state rollback. A blueprint places 1–64 parts together, with one revision, one request sequence, one complete geometry check and one aggregate inventory outcome. The first part anchors its structure; subsequent parts join that structure. A refusal on the final part preserves every original item, ID and component. The initial runtime offers the 19-piece starter room and undo of that room or the latest still-present individual placement. Whole-structure undo refuses any nonempty chest, refunds every piece and clears a registered bed within the removed structure. A general persistent edit history remains later work.

## Checks

The direct initial C++ run used the repository Nix GCC toolchain, C++20, `-O1 -Wall -Wextra -Werror`, the checked-out GoogleTest dependency, and no graphics device. It passed 15 inventory/session/codec cases together with five native-storage cases in 47 ms after compilation. The later [strict Bazel run](checks/bazel-focused-r03-summary.json) passed all 36 shared adventure cases, including atomic blueprint creation and complete-room refund/refusal. [Retained logs and XML](checks/artifacts.json) identify each run separately.

Cases cover atomic stack overflow/refund, splitting/merging, starter restore, refused/cancelled geometry preparation, stale/replayed/foreign-owner commits, movement invalidation, chest creation and contents, full/stale transfers, full output/refund refusal, crafting/equipment/resource depletion, bed recovery, all four structures with 256 parts each, lossless counters beyond JavaScript integer precision, codec corruption/identity/version refusal, duplicate IDs and missing functional state.

The final [capacity run](checks/capacity-r01-summary.json) passed **37/37 shared adventure cases in 180 ms**, including 19 inventory/session/codec cases. Its new capacity case took 42 ms and established:

- **4 structures, 1,024 parts and 32 functional components**, with 256 parts per structure.
- Four usable starter homes at four yaw rotations, each with a grounded tiled yard and additional storage. Real shared terrain, support, overlap, shelter and recovery-space rules accepted the installed geometry.
- **1,068 compiled collision solids** and a **31,448-byte canonical save**, preserving the exact accepted state and bytes through decode/re-encode.
- Material conservation across all charged parts, backpack stock and populated chests, plus refusal when exceeding part/component capacity.

This is a CPU authority, geometry and storage engineering workload on synthetic flat terrain. The fixture begins with expanded already-owned inventory to represent later accumulated supplies; it does not add an in-game resource grant or bypass accounting. It proves neither GPU frame rate nor the ordinary player journey. Integrated controls, rendering and restart evidence remain separate.

The first direct compilation exposed misleading one-line indentation in new source/tests under `-Werror`; formatting was corrected before the passing run. No warning flags or assertions were weakened.
