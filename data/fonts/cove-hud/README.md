# Native Cove text

The unmodified `DejaVuSans.ttf` is DejaVu Sans 2.37, copied from the installed
Debian `fonts-dejavu-core` package. Source: https://dejavu-fonts.github.io/.
The Bitstream Vera license and its copyright/trademark notices are retained in
`LICENSE.debian`; the separate Debian packaging paragraph does not license the
font. DejaVu's additions are public domain. No AI-generated text or font is used.

`python3 tools/make_cove_hud_font.py` uses Pillow to rasterize printable ASCII at
32 px into a fixed 512 x 256, single-channel coverage atlas. The exact Pillow
version, font hash and decoded atlas hash are in the generated
`src/render/cove_hud_font.inc`. `--check` verifies the source and generated output.
The RLE atlas and glyph metrics are compiled into the native HUD; there is no
runtime dependency on fonts installed on the player's computer. Glyph shapes
are not edited. The generated font is named Cove HUD, not Bitstream or Vera.

The UI uses normal mixed-case words, generous spacing, a solid dark backing and
at least 20 px body type at supported desktop sizes. The atlas is a coverage
mask, not a color texture, and never passes through world lighting or tonemapping.
