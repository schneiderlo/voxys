# Clean CPU component measurements

Median of three per-run quantiles/scalars. Times are microseconds except accepted-edit rows, which use milliseconds. Throughput includes replay observation bookkeeping. Peak RSS is whole-process peak, including startup and retained goldens. No displayed FPS claim.

## Accepted-edit update latency

Each triple is p50 / p95 / p99 in ms. Approximately ten samples per run means p95/p99 are observed maxima, not established population-tail estimates.

| Parts | Baseline | Village only | Combined | Samples/repeat (B; V; F) |
|---:|---:|---:|---:|---|
| 0 | 12.06 / 12.72 / 12.72 | 2.93 / 3.11 / 3.11 | 2.90 / 3.19 / 3.19 | 10/10/10; 10/10/10; 10/10/10 |
| 64 | 11.60 / 12.17 / 12.17 | 3.10 / 3.35 / 3.35 | 3.07 / 3.37 / 3.37 | 10/10/10; 10/10/10; 10/10/10 |
| 256 | 12.13 / 12.67 / 12.67 | 3.59 / 3.90 / 3.90 | 3.53 / 3.71 / 3.71 | 10/10/10; 10/10/10; 10/10/10 |
| 768 | 14.18 / 15.71 / 15.71 | 5.33 / 6.57 / 6.57 | 5.17 / 6.65 / 6.65 | 10/10/10; 10/10/10; 10/10/10 |

## All-update latency, throughput and memory

Latency triples are p50 / p95 / p99 in µs. Arrows show baseline → combined.

| Parts | Replay | Baseline latency | Combined latency | Updates/s | Peak RSS MiB |
|---:|---|---:|---:|---:|---:|
| 0 | idle | 10.79 / 12.04 / 15.22 | 10.85 / 11.63 / 15.83 | 66,770.38 → 67,000.28 | 265.16 → 264.81 |
| 0 | edit | 12.07 / 40.09 / 53.31 | 10.52 / 34.92 / 45.80 | 17,318.42 → 37,337.10 | 265.11 → 265.18 |
| 64 | idle | 11.02 / 11.35 / 15.64 | 11.04 / 11.66 / 17.00 | 63,479.60 → 63,794.25 | 264.74 → 264.96 |
| 64 | edit | 11.66 / 39.27 / 86.35 | 10.76 / 36.62 / 81.44 | 17,286.68 → 32,155.86 | 265.05 → 265.29 |
| 256 | idle | 11.35 / 12.51 / 17.29 | 11.32 / 11.62 / 16.28 | 55,990.83 → 56,057.76 | 265.29 → 265.32 |
| 256 | edit | 11.75 / 38.38 / 263.98 | 11.30 / 36.94 / 224.20 | 13,403.24 → 22,014.04 | 264.87 → 265.02 |
| 768 | idle | 12.00 / 12.27 / 16.46 | 12.02 / 13.11 / 19.47 | 42,759.17 → 42,216.54 | 264.80 → 265.07 |
| 768 | edit | 12.11 / 37.51 / 941.73 | 12.20 / 37.21 / 763.44 | 8,515.27 → 11,902.06 | 277.76 → 278.00 |

## Serialization latency

Triples are p50 / p95 / p99 in µs.

| Parts | Replay | Baseline | Combined |
|---:|---|---:|---:|
| 0 | idle | 20.05 / 24.70 / 32.21 | 20.06 / 23.02 / 34.60 |
| 0 | edit | 23.23 / 29.84 / 72.92 | 20.20 / 28.51 / 56.97 |
| 64 | idle | 20.34 / 25.79 / 38.51 | 20.48 / 25.78 / 40.42 |
| 64 | edit | 22.70 / 35.41 / 102.83 | 21.15 / 33.34 / 99.44 |
| 256 | idle | 20.78 / 30.40 / 42.10 | 21.00 / 26.68 / 38.07 |
| 256 | edit | 23.30 / 43.23 / 202.33 | 22.06 / 32.21 / 187.66 |
| 768 | idle | 24.07 / 35.30 / 48.75 | 24.07 / 40.48 / 52.58 |
| 768 | edit | 24.88 / 44.96 / 473.30 | 25.00 / 45.51 / 438.12 |

## Oracle and tests

72 metric records; 144 golden files; 144 exact cross-variant file comparisons. Measurement/golden guard failures: 0.

Original native aggregate: 2628 tests; 2606 passed, 2 failed, 20 skipped; 5 disabled.

- Failed: `CannonPhysicsSceneGpu.ThrownBrickFlightBounceAndStackMatchJolt`
- Failed: `FixtureGPU.CoveMoldedMachineryFitsCurrentOwnerBudget`
- Skipped: `AdventureBlueprints.GenericEarnedRecipeBuildsTheProvenFullTerrainShortcutWithoutChangingItsSite`
- Skipped: `AdventureEncounters.InstalledFullTerrainHasBothWalkingEncountersAndPreservesLegacyPoses`
- Skipped: `AdventureHudGpu.CaptureSharedRendererOverCleanGameScreenshotWhenRequested`
- Skipped: `AdventureRuntimeIntegration.FullTerrainStartupIdleInputAndSnapshotRestoreRemainAuthoritative`
- Skipped: `AdventureTrailSites.FullTerrainPaysForThreePierShortcutAndWalksBothDirectionsAndLongDetour`
- Skipped: `CreativeForest.RealTerrainKeepsDistantWoodlandAndBoundedCollision`
- Skipped: `CreativeScenery.ActualStartingShelfSupportsAllSixKinds`
- Skipped: `CreativeScenery.RealWorldCoverageExtendsBeyondStartingClearing`
- Skipped: `CreativeVillageRealTerrain.AllBuildingsAdmitAndCreativeFigureWalksConnectedApproachAndEveryCottage`
- Skipped: `CreativeVillageRealTerrain.MotorcycleTraversesVillageLanePastWellWithoutSnagging`
- Skipped: `CreativeVillageRealTerrain.OwnedConstructionRemovesWholeBuildingAndDoesNotRespawnItDuringSession`
- Skipped: `FreeBuildRuntimeIntegration.CreativeStartupPlacementAndExactRestore`
- Skipped: `GpuAuthoredShapes.OptionalActualWallPairContactReplay`
- Skipped: `GpuBroadPhaseOverflowBenchmark.DenseGridCandidateClamp`
- Skipped: `RaycastPathGPUTest.DayNightTerrainBenchmark`
- Skipped: `RaycastPathGPUTest.LegoHorizonBenchmark`
- Skipped: `SourceHouse/ImportedWallRuntimeIntegration.CannonInputAndFullWorldGpuAdmission/CannonImpact`
- Skipped: `SourceHouse/ImportedWallRuntimeIntegration.CannonInputAndFullWorldGpuAdmission/ReleaseConnections`
- Skipped: `SourceHouse/ImportedWallRuntimeIntegration.CannonInputAndFullWorldGpuAdmission/RemoveSupport`
- Skipped: `WindowGLFWTest.WindowCreation`

These measurements describe the retained benchmark binary snapshots and original aggregate binary; they do not establish behavior of later concurrent workspace changes. Candidate focused results, including any failures, remain separate in test-summary.json. Full raw per-run values, all intermediate medians and all comparator failures are in clean-summary.json; golden hashes and exact byte-comparison results are in golden-manifest.json.
