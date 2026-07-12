# Terrain Material Sources

These color maps are the 1K JPG versions of ambientCG assets:

- [Grass 001](https://ambientcg.com/view?id=Grass001)
- [Ground 037](https://ambientcg.com/view?id=Ground037)
- [Ground 054](https://ambientcg.com/view?id=Ground054)
- [Rock 050](https://ambientcg.com/view?id=Rock050)
- [Snow 001](https://ambientcg.com/view?id=Snow001)

ambientCG releases these assets under the Creative Commons CC0 license. Attribution
is not required. The links are retained for provenance.

`tools/generate_terrain_albedo.py` blends these unlit color maps by terrain height
and slope. The runtime shader remains responsible for lighting, shadows, fog, and
water.

The current terrain bake uses a 4K output for the 8K heightmap. UV alignment is
unchanged, while browser decode and GPU memory are reduced by 75 percent.
