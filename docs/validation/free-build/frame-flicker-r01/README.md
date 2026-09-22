# Whole-scene flicker — 2026-09-18

The application acquired the presentation texture before asking GPU physics to
reserve its submission. A temporary `Busy` or `NotReady` response then returned
without drawing. Browser WebGPU presents an acquired canvas texture at the end
of the frame even without an explicit native-style present call, exposing a
blank image during these waits.

The application now obtains physics admission first. No presentation texture is
acquired during a wait; the previous image remains visible. Surface acquisition
failure still releases the reservation through the existing frame guard.
Additive telemetry counts physics deferrals and successful surface acquisitions.

## Verification

- Rebuilt the browser WASM target and native tests.
- The new real-GPU regression holds an actual physics submission reservation,
  forces three Busy frames, checks that no surface texture is acquired, releases
  the reservation, and checks that rendering resumes.
- Restoring the old acquisition order makes that regression fail on all three
  undrawn texture acquisitions (`before-fix-test.txt`). The fixed order passes.
- 21 application and GPU tests pass, process exit 0 (`after-fix-tests.txt`).
- A separate rebuilt browser preview recorded 132 one-second observations:
  9,498 frame attempts, 13 physics deferrals, 9,485 acquired textures, and 9,485
  rendered geometry frames. No sample had an acquired-but-undrawn texture.
  Browser warning/error log was empty. This is a startup/idle preview check,
  not a claim that all possible rendering issues are covered.
- The original unsaved user session was not reloaded or modified.

The GPU test is opt-in, following the existing window-test convention:

```sh
nix-shell --run '/tmp/voxys-forest-native/bin/voxy_tests --gtest_also_run_disabled_tests --gtest_filter="ApplicationGPUTest.DISABLED_PhysicsAdmissionWaitDoesNotAcquireSurface:ApplicationTest.*:ApplicationConfigTest.*:ApplicationStatsTest.*:ApplicationDayNightTest.*"'
```

The final run used the native Wayland display. Attempts to wrap it in Xvfb
encountered a system/Nix EGL-library conflict; running directly removed the
wrapper error. No production graphics settings were changed for the fix.
