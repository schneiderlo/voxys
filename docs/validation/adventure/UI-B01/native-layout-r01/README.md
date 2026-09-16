# Native adventure interface: CPU layout check

The new adventure presentation uses warm ivory, brown and moss colors with the
existing font atlas, fixed quad buffer and blend pipeline. The original Cove
layout remains the default. Explore has a compact objective, contextual action
and quickbar. Build has a selected-part tray or category picker. Dialogue,
workbench, chest, Journal, bag and Pause use focused sheets with a selected-row
window and pointer navigation. Hits retain the runtime's displayed action and
intent token; this renderer does not interpret or grant game commands.

Five new layout cases passed in 16 ms. They cover 100%, 125% and 150% text,
640–1920px Explore/catalog/build views and 480px focused sheets; all pointer
targets are at least 44px, selected choices remain present, and no hit rectangles
overlap. The 640×480 / 150% build case caught seven uniform-width buttons taking
three rows. Packing by each measured label width restored two rows and the clear
aiming area. Existing adventure checks passed 63/63 in 245 ms, including real
placement validation in the quest home fixtures. Existing Cove CPU interface
checks passed 15/15 in 20 ms. The strict default-mode runtime build passed.

Commands from the repository root, inside the Nix shell:

```sh
bazel test //tests:adventure //tests:adventure_hud_layout //tests:cove_hud_layout //src/game:adventure_runtime --test_arg='--gtest_filter=-CoveHudGpu.*' --test_output=errors --jobs=4
# Only the affected targets were repeated after the layout fix:
bazel test //tests:adventure_hud_layout //src/game:adventure_runtime --test_output=errors --jobs=4
```

[Results and source hashes](results.json) index the evidence. Compressed logs
and XML are in `checks/`. The first opt-mode invocation stopped at an existing
GCC 15 optimized `-Werror=null-dereference` warning in
`fixture_registry.cpp:367`, during a `PartDefinition` vector copy. That failed
attempt is retained; it was not treated as an optimized build pass or fixed by
relaxing warnings.

These are CPU layout and compilation checks. No GPU game, screenshot, controller
journey or owner feedback is claimed here. Current native part cards illustrate
the installed catalog solid boxes; they are accurate shape diagrams, not actual
material/stud mesh thumbnails. The shared mesh thumbnail asset and integrated
native/browser usability journey remain UI-B01 acceptance work.
