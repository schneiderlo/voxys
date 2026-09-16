# LEGO-style cast and village: bounded integration

The adventure now renders the original toy minifigure player and three distinct
residents, together with the Meadow cottage, Market shelter, two trees, planters
and paths. The character correction is actual authored geometry: yellow round
heads, simple smiling faces, molded hair/headwear, block clothing and curved
hands. The legacy Cove character remains separate.

Preview: [open the new village](http://127.0.0.1:42754/index.html?experience=adventure).
Package `build-adventure-g-b/web-r03`, build ID
`adventure-51440f5c75b42947`, server session 89077. The previous G-A publication
and the earlier G-B test world are preserved. This is an uncommitted development
checkpoint, not completion or publication of G-B.

## Verified in this package

- Native and browser executables build. Targeted adventure and resident asset
  suites pass. The loader verifies each role's identity and refuses substitution.
- Fifteen preloaded adventure files match their current installed bytes,
  including the base figure, all three resident models/manifests and village
  props. [Exact payload bindings](preload.json).
- One changed-scene browser screenshot was inspected: all four toy people and
  the two shelters/dressing rendered together on the studded terrain. No repeated
  camera capture was used. This is initial village art, not owner approval.
- Ordinary pointer conversation with Moss accepted “A Place to Return.” Escape
  returned focus to the canvas. Keyboard B entered building; stock, costs and
  the disabled invalid-placement button were visible. Done returned to play.
- Ordinary Save adventure confirmed storage. A real reload of world
  `f350305bf389f645d61236b25d4d2613` restored the active home objective and the
  nearby Talk to Moss action. No hidden state manipulation was used.

[Machine-readable record and final source hashes](record.json). Compressed
compiler and targeted test evidence is in `checks/`. Asset production evidence
is in the [resident package](../../../../../data/adventure/residents-r01/README.md)
and [village prop package](../../../../../data/adventure/village-props-r01/README.md).

The [actual-terrain controller checks](../../WARM-B01/village-r01/README.md)
separately cover both shelter entrances, stairs, preserved interior save poses,
old buildings and old doorway approach clearance. The
[native mesh-thumbnail checks](../../UI-B01/native-mesh-thumbnails-r01/README.md)
separately cover CPU layout, GPU color samples and resource lifetime, including
the subsequent browser-header compatibility fix.

## Still required for the gameplay gate

This bounded browser check did not place a new house, complete the home quest,
craft/equip the compass or traverse a doorway through ordinary game controls.
The full native/browser journey, controller routing, large text/narrow viewport
checks and owner feedback remain open in `GAME_IMPLEMENTATION_TODO.md`. No
moving-game frame-rate claim, final visual approval or completed gate is implied.
