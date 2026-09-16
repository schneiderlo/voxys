# Prepared native home quest check

**Prepared; not executed.**
`scripts/validate_native_adventure_quest.py` is the native counterpart to the
[prepared browser journey](browser-driver.md). Source preparation and syntax
validation are not a G-B gameplay pass.

## Permission and setup

The current CUA surface cannot operate native windows. Obtain the owner's
explicit permission to run this prepared X11 UI driver outside CUA before
execution. The required `--authorized-ui-driver` flag acknowledges that prior
permission; the flag does not grant it. Do not execute this recipe merely because
the native CUA surface is unavailable.

Use an already built native executable containing the current village, resident
assets, home quest, pictured interface, corrected camera-relative movement,
corrected logical/framebuffer pointer mapping, and first-row Starter room in the
catalog. Run from the repository's Nix environment. Keep that executable,
`adventure.cfg`, installed assets and input helper sources fixed throughout the
journey. Do not run another GPU application check at the same time.

The display must provide an accessible X11 connection. The driver explicitly
requests the X11 window backend and requires a window whose `_NET_WM_PID` belongs
to its own child. A fallback backend without an owned X11 window fails. The owned
window is raised/focused once at each process startup. Subsequent loss of focus
fails the journey; the script does not operate an unrelated window or silently
take focus back.

Supply a new output directory and a separate new world directory. Their parents
must already exist. Neither directory may contain the other, and existing output
or saved-world directories are refused. The driver creates private directories;
it never opens the user's saved world, a prior G-A world, or an existing native
game process.

After explicit execution authorization, use paths appropriate to the machine:

```sh
nix-shell --run 'python3 scripts/validate_native_adventure_quest.py \
  --authorized-ui-driver \
  --binary /absolute/path/to/voxy_native \
  --output /absolute/path/to/new/quest-native-r01 \
  --storage-root /absolute/path/to/new/quest-native-worlds-r01 \
  --seconds 900'
```

The driver itself launches the binary with `--config adventure.cfg --uncapped
--width 1280 --height 800`, from the repository root. It sets
`VOXY_WINDOW_BACKEND=x11`, the isolated `VOXY_ADVENTURE_ROOT`, and a distinct
`VOXY_ADVENTURE_OBSERVE` file for each process. The first launch uses
`VOXY_ADVENTURE_NEW=1`. Only after confirmed Save does it terminate its owned
process and reopen the same isolated namespace with `VOXY_ADVENTURE_WORLD`.

## Journey

1. Confirm the empty world and a short D movement toward actual screen-right.
   Approach Moss's admitted position and accept **A Place to Return**.
2. Walk west through (−68.5, −892), (−76, −892), (−76, −895), avoiding the new
   cottage. Press B, then Tab. Require **Starter room** as the first current
   catalog choice with its displayed cost. Select it with arrows and Enter.
3. Aim near (−85, −146.24, −895). Allow at most four Page Up steps, only when the
   game asks to raise the foundation. Require a valid yaw-zero preview, exactly
   19 accepted parts and payment of 70 wood, 16 stone and 8 scrap.
4. Use the actual accepted preview as the house origin. Walk around to its
   doorway and inside. Store, take, and finally leave ten wood in its chest.
   Open the workbench, confirm that the compass is locked, then craft the field
   hammer for 4 wood and 2 scrap and verify automatic equipping. Use the sheltered
   bed to register a ready home.
5. Return to Moss and select **Complete quest: learn compass**. Require one
   permanent recipe receipt with no free materials or compass. Reopen dialogue;
   completion must be absent and **See you soon** must grant nothing further.
6. Return to the home workbench. Pay 2 wood and 4 scrap for **Craft trail
   compass**. Verify inactive backpack ownership, then select **Equip trail
   compass** and require exactly one equipped compass with no backpack copy.
7. Verify actual bearing/distance to the old beacon at (−63, −975), then move
   and verify the new distance. Open Pause → **Open bag**; use the named compass
   target rows to check home and then restore the beacon target.
8. Open Pause → **Save adventure** and wait for confirmed Save. Read the two
   durable copies and require byte equality. Restart the same world, compare
   exact observable ownership/progression and near-exact player position, and
   require the confirmed copies to remain unchanged by opening the world.
9. Revisit Moss after restart. The quest remains completed; the receipt and
   observed ownership remain unchanged.

This is one bounded journey. Each walk permits at most 240 short input pulses
and fails after nine stalled pulses. Framing allows at most six ordinary right
drags. The overall limit accepts 180–1,200 seconds, checked between actions and
observation waits; owned-process cleanup can add up to 20 seconds. The driver
does not restart failed stages, invent another route, recover/teleport the
player or resume an earlier run automatically.

## Input and observation contract

The script reuses `Controls`, the X11 event layouts and real camera projection
from the existing native adventure helpers. Keyboard events use GLFW's physical
letter mapping and target only the owned child window. Native menu navigation
uses Up/Down and Enter. The displayed row's stable intent is read and checked
for context changes, but is **never submitted as a direct game command**.

Walking derives screen-right from row zero of the actual view-projection matrix
and forward from the horizontal camera eye-to-target vector. It does not use
the old reversed yaw formula. Ground aiming projects into framebuffer pixels,
then converts each axis to X11 logical client coordinates using the actual
window dimensions. It verifies both raw logical pointer and used framebuffer
aim observations. A HUD-owned or captured pointer cannot qualify as world aim.

Pointer delivery uses the existing owned-window warp followed, if necessary,
by a recorded standard X11 MotionNotify event. Right-drag uses X11 button and
motion events. These are automated window input events, not physical hardware
input or a CUA native journey. No gamepad device is opened or created. Native
controller navigation, physical-controller usability, browser DOM buttons and
the browser's double-click guard are not claimed by this keyboard journey.

`VOXY_ADVENTURE_OBSERVE` is a read-only JSON output from the production runtime.
The driver does not use exported actions, game-state setters, debug cameras,
artificial inventory, save editing or direct session methods. Standard Save
and reopening the saved namespace are the only persistence actions. Process
termination follows confirmed Save; it does not verify an in-game Quit flow.

## Output and acceptance limits

The new output directory retains `summary.json`, `executed-driver.py`, both
process logs, final read-only observations, stage/input traces, active failed
walks, checkpoint hashes, and binary/config/helper hashes. The separate world
directory retains the actual save. Errors, forced termination or matching
native/GPU error lines fail the result. Failed evidence stays in place; a
corrected executable or journey requires a separately named authorized run.

Reload comparison covers every observed part, component material total,
backpack material total, hammer/compass equipment, registered bed, met-resident
mask, schema, quest phase, unlocked recipe and reward receipt. This fresh
journey creates no other item kinds. Hidden item-slot metadata is not exposed
by the observer. Equal confirmed current/mirror bytes and unchanged copies on
restart do not prove every hidden field was restored into runtime state.

There are no screenshots, video, controller claims, default captures, visual
approval, performance results or complete gate claims. The first authorized run
may expose a route, timing, display or input-delivery defect; syntax validation
cannot establish that this prepared driver completes successfully.

Source-only validation, which opens no UI or device:

```sh
python3 - <<'PY'
import ast
from pathlib import Path
path = Path('scripts/validate_native_adventure_quest.py')
ast.parse(path.read_text(), filename=str(path))
print('Syntax valid; journey not executed')
PY
```
