# Native home journey — ordinary controls

The native home journey passed on the full installed landscape at 1280×800.
This is focused G-A evidence, not a declaration that the complete gate passed.
The driver sent ordinary X11 keyboard, pointer and mouse-button events to its
owned GLFW window. It only read the observation JSON and real save replicas.
It did not inject accepted game state, edit saves or take screenshots.

This original run checked gameplay authority and controls before the browser
visual review found missing opaque-scene wiring and an incorrect depth source.
It does not prove that the original binary displayed the accepted house. The
[later native renderer check](../native-render-r01/README.md) reopened this same
genuine save after those fixes; the browser image supplies visual acceptance.

## Completed journey

The [r02 record](r02-summary.json.gz) proves starting at town, walking to a free
site and spending **70 wood, 16 stone and 8 scrap** on a 19-part starter room.
Its actual origin is **(−84, −145.92, −895)**. The room contains individual
foundations, walls, an open doorway, roof, bed, chest and workbench.

The player then manually placed a separate foundation and floor, removed the
floor, placed it again, undid that placement and removed the foundation. All
materials returned exactly to the preceding balance. The player walked through
the room's real doorway and stored and retrieved ten wood from the chest.

The [r03 record](r03-summary.json.gz) resumes the confirmed manual-building save
instead of reconstructing the house. It repeats the interior approach, uses the
workbench to craft and equip a field hammer for four wood and two scrap, and
registers the sheltered bed. It saves, stops the native process, starts another
process with the same world ID and compares the exact structures, furniture,
inventory, registered bed and equipped tool. Player position differs by less
than .03 m. Final balances are **566 wood, 304 stone and 70 scrap**.

The chest's final slots are empty: this proves real store/take behavior and
restoration of the empty final state. It does not claim a nonempty chest stack
survived this restart. That requires separate native or browser evidence.

Both r03 processes stopped after requested SIGTERM, without force killing or
logged validation/error lines. The passing continuation took 64.244 seconds;
r02 took 130.579 seconds including its bounded failure described below.

## What failed and what changed

The [r01 record](r01-summary.json.gz) stops on its first missing ground preview.
Read-only pointer telemetry added afterward showed that XWarpPointer could leave
the owned XWayland client's pointer at (0,0). In r02, one ordinary MotionNotify
event delivered the requested (624,355) pixel, and the same ground ray then hit.
The driver uses this event only when observed pointer delivery is missing.
There were five such fallbacks in r02. CPU reproductions of the original ray
were complete and hit the terrain; no physics-solver tolerance was changed.

r02 stopped because its loose bench approach left the chest nearest to the
player, so E correctly reopened the chest. The driver was corrected to approach
the bench at room-relative X −.58, Z +1.25, within .08 m, and the bed at
X −.1, Z −.5, within .06 m. r03 then completed from the genuine saved checkpoint.
No game source change or fabricated recovery state was used for this correction.

## Reproduction and identity

The owned driver is `scripts/validate_native_adventure.py`; frozen executed
copies, full records and process logs are retained here. A fresh journey uses:

```sh
python3 scripts/validate_native_adventure.py \
  --binary build-native-save-host/bin/voxy_native \
  --output /tmp/adventure-journey-new \
  --storage-root /tmp/adventure-worlds-new --seconds 600
```

A continuation supplies `--continue-from` pointing at a prior recorded run and
the same storage root; it trusts only stages covered by a confirmed save.
The process environment sets `VOXY_WINDOW_BACKEND=x11`, the isolated storage
root and a read-only observation output path. It uses `adventure.cfg`.

- Tested native binary SHA-256: `756ff2de67d4c9204410cb0c001548dd90be6987459295b6c9eba57a1193a0cd`.
- Passing driver SHA-256: `22c4705ddf9e3b8362cf34bed0209744d8deff13e0445424c29f9bfdc432e6b5`.
- World ID: `bf7488a44a5e0be04c0ee231ac98288d`.
- Final save replicas: 1,303 bytes each, SHA-256 `494eba13137e22f49833ba12d5aa2a7c0c935b9823921f84e472bee85a390d7d`.

The two final replicas and [artifact hashes](manifest.json) are retained. These
artifacts describe the tested binary; later presentation or cleanup changes
must be identified separately. The retained working save is under
`build-adventure-g-a/native-worlds-r02/adventure-v1/`.
