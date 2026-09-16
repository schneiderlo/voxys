# Original town content, r01

This is the original authoring/reference source for Moss, Rivet, Lumen, **A Place to Return**, and the **Trail compass**. It was adapted from the reviewed G-B draft after the G-A gate commit `2d6d0bc154169fc8d73d744ad9392b9c481d0bfd`. No external character, texture or model was used; the residents reuse this project's original robot rig and existing materials.

The game currently uses compiled C++ definitions in `src/game/adventure/town_residents.*` and the root-owned adventure runtime/session. This JSON is not a runtime scripting system or an independently parsed authority. Keep compiled dialogue, IDs, costs and rules aligned with this source when integration changes them.

Resident IDs are **1 Moss**, **2 Rivet**, **3 Lumen**. Their canonical anchors remain inside the existing five-metre protected town space. Collision uses installed identifiers separate from player parts, with legacy identifier collisions deferring the affected resident. Ground support and bounded admission preserve valid old player poses and houses. The two optional Beam seats remain proposals until a runtime implementation actually installs them.

The home quest recognizes an existing, currently usable registered bed plus Chest and Workbench in the same structure. Empty storage is sufficient. Dialogue prioritizes Completed, then NotAccepted, then readiness of an active quest. A permanent reward receipt is the sole source of recipe availability; Ready and UI eligibility are derived.

The compass costs **2 wood + 4 scrap**, is ItemKind **5**, and activates only when actual item ownership has moved atomically into `equippedUtility`. Backpack possession alone grants no bearing. The separate field hammer remains equipped. Known G-A saves require the explicit version migration owned by the session implementation.

Web presentation uses the common runtime `rows` and action10. Journal is action16, utility equip/unequip is action17 (actual backpack slot0..23 or255), and target toggle is action18. Root emits authoritative text, state, capacity and availability. The browser does not grant rewards, infer missing ownership, or create NPC interaction actions from resident coordinates.

This file records design/provenance, not completed game validation. Native/browser player journeys, migration checks and a representative inhabited-town view belong to the G-B gate evidence.
