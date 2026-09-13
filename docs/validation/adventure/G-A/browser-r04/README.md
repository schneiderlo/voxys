# G-A browser functional journey, r04

**Functional journey passed. Visual review failed. G-A is not passed by this record.**

Executed package `adventure-09f980b7b4d32b6b` from `build-adventure-g-a/web-r02` on isolated local port 42752. The retained test-only browser profile is `/tmp/voxys-adventure-browser-profile-r04`; no user profile was used. The complete ordinary-input journey took 45.207 seconds. Chrome 152.0.0.0 selected an AMD RDNA-3 hardware adapter (not fallback). WASM heap was 536,870,912 bytes at entry and completion. No uncaptured/validation GPU errors or page exceptions were reported. The owned browser closed normally.

## Functional evidence

[summary.json](summary.json) records actual keyboard/mouse/DOM inputs and read-only runtime observations. [executed-driver.mjs](executed-driver.mjs) is the exact executed script. [package-sha256.json](package-sha256.json) pins every delivered file. No exported game actions, state setters, archive edits, fabricated resources, camera setters or injected input events were used.

- Gathered an installed wood pile: actual inventory increased by 12.
- Placed the 19-piece starter room at (-84, -145.92, -895), charged exactly 70 wood / 16 stone / 8 scrap.
- Placed a separate foundation and floor, removed the floor, replaced and undid it, then removed the foundation. Actual inventory returned exactly to its initial balance.
- Walked through the empty doorway into the real house.
- Stored and retrieved wood through the actual chest, then left ten wood inside.
- Crafted/equipped a field hammer at the workbench for four wood and two scrap.
- Registered the roofed bed as the recovery point.
- Used Save, then the ordinary Continue link. All structures/parts, the nonempty chest, supplies, bed and hammer restored exactly; player position matched within .03 m.
- Returned to the gathered pile after reload. It remained depleted and yielded no additional wood.

The bare default URL and direct canvas left-click were not exercised in this run. Pointer lock remained inactive throughout its observed completion. Follow-up checks must prove those paths explicitly.

## Visual failure

The single [captured view](home-exterior.png) shows terrain and HUD, but no house or robot. The captured matrix projects the house center to (739.36, 286.00) and robot center to (640.00, 433.42) inside the unobscured 1280 × 800 canvas; framing is correct. This is therefore not acceptable visual evidence for a playable house.

Independent source review found that Application registered the adventure scene callback but enabled BlitPath's opaque scene only for Cove. The callback is skipped without that allocation. Root is fixing this configuration and must verify the render-fixed package with a fresh meaningful image before closing G-A. The functional checks above remain valid; this record does not claim visible furniture, visual quality, or sustained performance.
