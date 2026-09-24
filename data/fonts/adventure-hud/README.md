# Adventure HUD font

DM Sans variable font, from [Google Fonts](https://github.com/google/fonts/tree/main/ofl/dmsans), downloaded on 2026-09-24 as `DMSans[opsz,wght].ttf`. This is the original TTF used to produce the existing browser font in `ui/assets`; it has not been converted or subset here.

- SHA256: `8cd08d97e89c24d0aa92edd2f0f4c8ee6195eee9b7c9f154865a58b02f0c1c0d`
- License: accompanying `OFL.txt`, SIL Open Font License.
- HUD atlas: ASCII 32–126 at 32 pixels, optical size 32, weight 550.

`tools/adventure_assets/bake_hud_art.py` rasterizes the actual medium weight with Pillow and embeds the glyph coverage in the shared native/browser art atlas. Font loading and text rasterization do not happen during gameplay. The existing Cove font remains independent.
