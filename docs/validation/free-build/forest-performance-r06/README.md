# Distant forest GPU budget

Checked on 2026-09-23 in headed Chrome 152 on an AMD RDNA 3 adapter. The
requested 1920 × 1080 window produced a 1946 × 1095 render surface. The
two-kilometre forest remained enabled in every row except the diagnostic
no-forest run.

| Scene | GPU frame interval | Visible trees | Colour triangles |
| --- | ---: | ---: | ---: |
| Previous full forest | 6.33 ms | 9,217 | 1,389,822 |
| Six-sided mesh, stable distance selection, earlier far LOD | 5.03 / 4.94 ms | 3,001 | 531,910 |
| No forest, diagnostic only | 4.40 ms | 0 | 288,782 |

The final candidate keeps full tree density within 400 metres and selects
farther trees with a deterministic identity hash. The selection probability
falls gradually with distance; it never removes the 2 km draw range. A
six-sided molded crown and two-tier pine retain distant silhouettes with
fewer triangles. The [startup view](startup.png) shows the resulting forest.

This is GPU headroom near 200 FPS, **not a measured sustained 200 FPS**. The
startup browser uses display-paced rendering, and its frame counter includes
loading. CPU and presentation timing still need a steady-state uncapped run
before claiming 200 FPS. Measurements depend on hardware and viewpoint.

Validation: the full application rendered without GPU errors in the browser;
the forest horizon and silhouette GPU tests, the forest collision regression,
and the WASM build passed. Source GLB/Blend and cooked VMesh checksums match
their checked-in provenance and manifest.
