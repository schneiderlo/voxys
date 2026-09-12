# Effects browser driver: bounded independent review

Reviewed `scripts/validate_cove_effects.mjs` and the corresponding Application JSON/effect attribution fields read-only. No Node run, app/browser/GPU process, screenshot or gameplay repetition was performed for this review.

Two concrete initial false-pass gaps were reported, then independently verified corrected:

- Aggregate splash counts could grow from boat/cargo entry or wet impacts while the robot crossed the water. The final stage snapshots and requires growth in `playerEntrySplashes`, which counts admitted Player water-entry sprites only, and also requires actual encoded instances.
- Filtered-lighting `environmentReady`/`environmentGpuBytes` did not prove authored scenery admission/drawing. The final driver checks the exact 39 collision proxies, 1,241,888 scenery bytes and matched submitted scenery draw/scene coherence observations, without requiring transient presentation fields during ordinary transition polling.

No further concrete issue found in the reviewed real-key dispatch, blueprint export, pause/Rescue flow, exact ownership/stock comparisons, held-key cleanup, or same-owner two-proof-point submission/completion checks. The driver correctly states that per-render effect counts are observational, not atomically tagged submission facts. Numeric renderer tests separately own pixel/depth proof. Runtime route reachability and final package success remain pending the root-owned single actual browser run.
