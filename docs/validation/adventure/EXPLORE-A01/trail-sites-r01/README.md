# Trail scenery and paid terrace route — CPU evidence

Five focused cases passed, with no skips, in 3.714 seconds. The full-terrain case
uses the unchanged 8192² raw landscape and production `AdventurePlayer`,
`AdventureSpatialQueries`, construction validator and session transactions.

The test first admits both existing markers, all eight village groups and all
three residents. All four trail groups and sites then admit. Their 23 rendered
catalog pieces compile to the same 23 published collision boxes. Tests also
cover old-player/home/recovery/identity priority, atomic capacity refusal,
discovery range and line of sight, unchanged resources 1–18, and a live owned
bed/chest/bench home without changing its recovery registration.

At X=29.5, Z=-923, the actual 0.96 m terrain face blocks ordinary walking. Three
Piers cost exactly six stone through `prepareBlueprint` and `commit`, with real
terrain/support/placement validation. Their bottom centres are
(29.10,-129.92,-923.48), (29.10,-129.92,-923), (29.10,-129.92,-922.52).
The actual player ascends and descends without jumping or leaving Walking mode.
The 37 m contour route through (29,-941), (30,-941) also works both ways.
Discovery 1 sits at the reached high side (30,-923); its frame sits at
(30,-920.5). Only unpublished schema4 content moved; old content stays exact.

The earlier 0.64 m face at X=21.5 proved walkable, and its proposed step failed.
That failed run is retained. The bounded diagnostic examined four real faces
and nine paid layouts; its nominal test exit is a diagnostic result, not a
passed gameplay acceptance. Final assertions use the successful geometry only
and retain an assertion documenting that the earlier ledge is walkable.

`results.json` records the evidence and exact source hashes. `checks/` contains
the final direct executable output/XML and successful strict focused build.
The ordinary Bazel run intentionally skips the opt-in full-terrain case; the
archived direct run supplied the raw path and ran all five cases without skips.

Reproduce from the repository development environment:

```sh
bazel test //tests:adventure_trail_sites
VOXY_ADVENTURE_TEST_TERRAIN=/absolute/path/to/unchanged-full.r16 \
  bazel-bin/tests/adventure_trail_sites --gtest_output=xml:/tmp/trail-sites.xml
```

This is CPU component integration. It does not prove the complete 407 m route,
camera visibility, ordinary native/browser input, rendering, save migration,
enemy play, accessible furniture reach, or the EXPLORE-A01/G-C gates. The field
home case proves live shelter and same-home furniture membership; the additional
physical furniture-access helper has separate evidence owned by its implementer.
