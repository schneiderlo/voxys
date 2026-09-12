# Mechanism GPU classifier correction

The failed case is `MeshPathGPUTest.NamedMechanismsKeepStationaryNodesAndMatchLiveBodyAtLargeSectors`. Evidence is `checks/gpu-pose-r02.log`: both mechanisms had zero classified stationary pixels, while moving pixels changed (456 rotor / 420 drum), live-body pixels matched exactly, and stale body generations produced no colored pixels.

The test incorrectly required red and blue to remain exactly zero after displaying unlit linear green `(0,1,0)`. The existing ACES input/output color matrices in `shaders/mesh_path.wgsl:447` mix channels before the sRGB output at line 626. With the test's default exposure 1, these equations produce approximately RGBA8 RGB `(148,228,89)`. The original classifier could therefore never recognize a correctly rendered stationary green node.

The correction uses `G > R + 70` and `G > B + 70`, matching the existing texture-quadrant classifier in `tests/test_mesh_path.cpp:582`. It is applied identically to live and mechanism-overlay test copies. The render path, geometry, materials, camera, lighting and all acceptance thresholds remain unchanged: more than 20 stationary pixels; exact RGB equality on the union of the before/after stationary masks; more than 100 changed color channels; at most 32 live/body differing color channels; zero colored pixels for a stale body generation.

Validation here is source inspection and evaluation of the existing color equations only. No GPU process, screenshot or test execution was performed for this correction. Root will rerun this single case and retain both the original failed log and the corrected result.
