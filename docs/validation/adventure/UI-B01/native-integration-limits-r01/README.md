# Native input acceptance limits

This is a read-only integration review for the current G-B interface. It adds no
test pass, game launch, screenshot or saved-world mutation. Earlier checks keep
their original scope; none were repeated for this review.

The current supported computer-control surface is the browser. Native surfaces
are disabled. The repository's X11 input drivers were inspected but **were not
used as an alternative control route**. A current native keyboard/controller
player journey therefore remains unverified.

## What exists in the code

| Facility | What it can establish | Limit |
| --- | --- | --- |
| `scripts/validate_native_adventure.py` | Historically drives an owned native window using keyboard/mouse events, observes the runtime, confirms a save, restarts the process and compares restored state. | Its journey targets the older G-A menus. It is neither current G-B evidence nor an available UI route in this task. |
| `scripts/cove_virtual_gamepad.py` and `scripts/validate_native_cove_ui.py` | An existing Linux OS-controller helper and a Cove journey demonstrate the repository's controller-input approach. | They do not test Adventure. Read-only host inspection found neither `/dev/uinput` nor `/dev/input`; no controller device was opened or created. |
| `tests/test_adventure_world.cpp`, `AdventureInput.ControllerMatchesHudAndPlacementCannotAlsoJump` | Exercises the production input router's jump, use and build bindings. | Supplies a router sample directly; it does not cover OS events, GLFW, rendered menu focus or `AdventureRuntime::update`. |
| `tests/test_adventure_menu.cpp` | Exercises stable menu intents, context ownership and duplicate-activation rejection. | Pure menu-authority tests do not prove native pointer or controller navigation. |
| `tests/test_native_adventure_saves.cpp` | Exercises storage publication, reopening and migration through the native storage owner. | Storage API checks do not prove that a user can reach and activate Save through the native interface. |

The actual native path is GLFW callbacks and `glfwGetGamepadState` in
`src/engine/platform/native/input.cpp`, frame input in `src/app/application.cpp`,
then `AdventureRuntime::update` and its rendered-menu intent handling in
`src/game/adventure/adventure_runtime.cpp`. Calling the session or runtime action
APIs directly bypasses that path and must not be labelled normal-control evidence.

There is no Adventure input-replay option in `src/core/config.cpp`.
`--inspection-motion` is an asset camera recipe. Physics and Wreckwater replay
modules replay their own simulations; they do not drive Adventure menus.
`VOXY_ADVENTURE_OBSERVE` writes a read-only runtime snapshot approximately every
quarter second. It accepts no input commands.

## Remaining native journey

When an authorized native input surface is available, a bounded journey should
use the current displayed choices: **Starter room**, catalog names with separate
cost details, and **Craft field hammer** inside the workbench sheet. The old
driver instead expects **Starter room blueprint**, cost text inside catalog
labels, and automatic crafting on interaction. It cannot be rerun unchanged as
current interface acceptance.

The current journey must cover native menu selection and activation, building,
resident dialogue, the home quest, crafting/equipping the compass, explicit save
confirmation and reopening the same world. Compare quest/reward state, utility
equipment and complete inventories as well as the earlier house, bed and player
pose checks. The observer does not expose every item slot; use confirmed save
content for the full persistence comparison. The historical driver terminates
its owned process after confirmed save, so its restart check does not establish
an in-game Quit flow.

A physical-controller usability check and an automated OS-controller check are
different evidence. Neither is claimed here. Browser controls and the existing
[native layout](../native-layout-r01/README.md) and
[mesh-thumbnail checks](../native-mesh-thumbnails-r01/README.md) remain useful,
but cannot substitute for the missing native player journey.
