# Independent receiver-plane PCF review

Read-only review of `shaders/scene_sun_shadow.wgsl.in`, its generated mesh/terrain consumers, and `tests/test_scene_sun_shadows.cpp`. No actionable finding.

For the existing orthographic projection, matrix rows are orthogonal. Dividing each normal/row dot product by that row’s squared length produces the inverse-transpose plane coefficients. The resulting UV depth gradient correctly accounts for the inverted UV Y axis. Each PCF reference follows the receiver plane; geometric-normal bias, caster raster bias, bilinear comparison, nine taps, border fade and resource ABI remain unchanged. The near-parallel denominator guard retains the existing constant-reference fallback without non-finite division.

The test uses an independent light-ray/world-plane intersection to author real rasterized depth. It compares the shipping function with a mutation restoring only the old constant reference, across256 subtexel phases. A separate actual6cm raised caster protects near-contact shadows. The queued readback is consumed after its map callback; callback payload ownership and resource destruction remain bounded. This is useful mathematical/numeric coverage, not a claim about every slope or final visual approval.

No compiler, test, GPU, app, screenshot or image review was performed by this independent reviewer. Runtime measurements and the final three-case batch belong to the accompanying owner evidence.
