# Compound wall solver convergence

After the verified friction/torque cache guard, the eight-patch synthetic source wall was tested at three solver substep counts. Only the fixture's `init.gpu.substeps` changed between runs. Gravity, damping, geometric slop, sleep thresholds, geometry, source graph and 1200-tick gate were unchanged.

| Substeps | Result | Released dynamic parts awake at end | Maximum linear speed | Maximum angular speed |
| --- | --- | --- | --- | --- |
| 4 | Fails after 1200 ticks | 28 / 29 | 0.126991 | 0.147701 |
| 8 | Fails after 1200 ticks | 14 / 29 | 0.0486288 | 0.0149443 |
| 16 | Passes; certified settled at 718 ticks | All accepted as sleeping by owner certification | Not sampled after static promotion | Not sampled after static promotion |

This is evidence of solver convergence in the actual mixed 39-part arrangement on a synthetic floor. It does not demonstrate the actual house remainder and terrain pass. Sixteen substeps are a diagnostic result, not an approved production setting. The test fixture has been restored to four substeps, and no production setting was modified.

The passing test verifies all 39 bindings, settled owner phase, certified promotion to static poses, and eligible collision cells above the synthetic floor. No force, impulse, forced sleeping or pose freezing was added to obtain this result.

Evidence: `cachefix-four-substeps.log/.xml`, `cachefix-eight-substeps.log/.xml`, `cachefix-sixteen-substeps.log/.xml`. Total isolated-test wall times were approximately 25.1s, 23.4s and 17.1s respectively, but include pipeline creation and different simulated tick counts; these are not comparable production frame-cost measurements. Evaluate actual-world performance and contact behavior before choosing a production solver budget.
