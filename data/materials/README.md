# Terrain Material Sources

These 1K JPG maps are from ambientCG assets:

- [Grass 001](https://ambientcg.com/view?id=Grass001)
- [Ground 037](https://ambientcg.com/view?id=Ground037)
- [Ground 054](https://ambientcg.com/view?id=Ground054)
- [Rock 050](https://ambientcg.com/view?id=Rock050)
- [Snow 001](https://ambientcg.com/view?id=Snow001)

ambientCG releases these assets under the Creative Commons CC0 license. Attribution
is not required. The links are retained for provenance.

`tools/generate_terrain_albedo.py` blends the color maps by terrain height and
slope for the low-frequency macro bake.

The current terrain bake uses a 4K output for the 8K heightmap. UV alignment is
unchanged, while browser decode and GPU memory are reduced by 75 percent.

The raycast path also loads Color, NormalGL, and Roughness maps from:

1. Ground 054: dry/wet sand and seabed.
2. Ground 037: soil and sparse ground cover.
3. Grass 001: dense grass.
4. Rock 050: exposed rock.

At startup, those maps become two four-layer RGBA8 arrays with complete mips:
an sRGB albedo array that WebGPU linearizes in hardware, and a linear NormalGL
XYZ plus perceptual-roughness array. The normal mips are renormalized and their
variance raises filtered roughness. Both arrays together use about 42.7 MiB of
GPU memory at 1024 square, including mips.

The runtime owns lighting, continuous shoreline/slope blending, wetness,
world-projected sampling, and rock triplanar projection. The original archive
copies remain under `data/generated/cc0_texture_pack/zips/`.
