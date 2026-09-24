# Shared game HUD artwork

The catalogue pictures and portrait render the actual installed building pieces and builder. The baker reads their `.vmesh` triangle positions, authored split normals and node transforms directly. Piece 15 combines the installed doorway frame with the real closed door leaf. The portrait evaluates the builder's hierarchy and applies its recorded render-to-canonical basis before taking a front portrait.

The building colors and material roughness match the runtime's warm palette. Studio lights reveal the existing bevels, studs and self-shadowing; they do not replace or embellish the geometry. CPU Cycles renders at three times the final size and Pillow downsamples the transparent results. The portrait preserves the builder's original materials.

`atlas.png` is the exact 1024×512 RGBA texture embedded in `src/render/generated/adventure_hud_art.hpp`. It contains:

- Fifteen 128×128 piece cells, eight across the first two rows, in piece-kind order.
- Three generated 64×64 action icons in the remaining cell: motorbike `(896,128)`, cannon `(960,128)` and save `(896,192)`.
- The 192×192 builder portrait at `(0,288)`.
- A transparent 256×256 reservation at `(256,256)` for the live minimap.
- DM Sans 550 glyph coverage in the 512×256 region at `(512,256)`.

`sprites/` contains individual transparent renders. `review.png` places the atlas over a dark background for inspection; it is not used by the game. `provenance.json` records the source, generator and output hashes, material policy, font settings and renderer version.

The three semantic action icons were generated with the built-in image tool. Its model version is not exposed. They are decorative controls, not catalogue pieces or substitutes for game geometry. `generated-controls/` retains the original unmodified RGBA outputs, exact prompts and their hashes. Packing preserves their alpha and performs only proportional downsampling. Rebuilding the atlas reuses these checked-in originals; it does not regenerate them.

Reproduce with Blender 4.1.1 and Pillow 11.1.0:

```sh
python3 tools/adventure_assets/bake_hud_art.py --blender /path/to/blender --samples 64 --threads 3
python3 tools/adventure_assets/bake_hud_art.py --check
```

The bake uses CPU rendering and CPU OpenImageDenoise. Gameplay only uploads and samples the finished atlas; it never invokes Blender or rasterizes fonts. The check verifies source/output integrity, the embedded PNG, font metrics and atlas reservations without rendering again.
