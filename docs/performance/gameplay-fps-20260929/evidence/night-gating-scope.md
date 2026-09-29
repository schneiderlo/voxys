# Zero-direct-light shader gate scope

Read-only configuration finding before a proposed night gameplay screen. No FPS harness or production shader was changed.

The temporary terrain/water shaders named `night-shadow-gating` test `camera.lightingColor.w == 0.0`. This field carries the shared direct sun/moon intensity, rather than a Boolean night state.

`src/render/day_night.hpp::sampleDayNight` selects moonlight when the sun direction's Y component is below -0.04. Midnight therefore uses the mirrored moon direction and `0.28 * moonlight`, yielding intensity 0.28 at hour 0. The exactly-zero gates do not trigger during ordinary midnight. They can trigger in the short unlit direction-switch interval near dawn/dusk, or when direct intensity is explicitly set to zero.

Setting `lighting.dayHour` while `lighting.dayNightEnabled` is false only stores the requested hour. `Application::updateDayNight` immediately returns when disabled. Disabling an enabled cycle restores `fixedLightingSettings_`, including sun intensity and direction. Consequently, requested CLI values such as `--hour=0 --day-night=0` do not establish actual night lighting or zero direct intensity.

A measured zero-intensity variant must record the actual renderer getter values at both endpoints and define its scenario explicitly. Enabling the cycle, selecting hour 0, and pausing it establishes ordinary moonlit midnight; a zero-intensity guard should reject that scene. Explicitly changing direct intensity to zero creates a moonless fixed-light diagnostic and stops the cycle through the existing driven-setting rule. Such a diagnostic cannot establish a benefit for normal midnight gameplay.

Mesh shadow gating also excludes surfaces with zero direct N-dot-L, so that candidate remains relevant in daytime and moonlit scenes. The exactly-zero terrain/water shortcut is narrower. The conservative daytime combination (mesh gating + deferred water shadow + lazy Cove background load) is a better general-workload candidate for the next clean game screen.

The prepared combined-night Intel oracle uses sun intensity 0 and hour 0 intentionally; its exact image proof describes that controlled zero-direct-light fixture, not the engine's ordinary hour-0 palette. Its manifest is still available as a diagnostic candidate. No Intel oracle or clean FPS screen has been run for it at the time of this finding.
