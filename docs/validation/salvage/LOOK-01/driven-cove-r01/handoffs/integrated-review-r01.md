# Final integrated D39–D43 source review

Reviewed 2026-09-12 06:00:20 UTC against `411fa1cf`. **No remaining actionable correctness findings in the reviewed integration.**

The review covered the combined live Application changes, launch shape helper and cancellation path, native recovery/pause guidance, browser objective routing, F9/surface integration, and the native delivery-to-power driver. Earlier renderer/art and focused test reviews were used as context; unchanged deep validation matrices were not repeated.

- **D43 launch:** `prepareBuild` retains the compiled candidate without taking GPU ownership. `pollBuild` executes through the existing preparation adapter after the resource owner is polled. Busy/NotReady remains Pending; successful roots are skipped on retry; moved payloads are cleared only when every root has a handle. Every valid returned handle is stored even alongside an error. Terminal rejection marks the retained candidate canceled; normal cancellation retires its handles and waits for Missing. Existing fatal GPU failure handling is unchanged. Actual Ready, joined/completed staging, atomic parent replacement and observed parent death are still required before publication.
- **D39/D40 integration:** submitted-frame mechanism state keeps the rollback guard, exact live-body matching and validated rope baseline across command acknowledgements. Static dock markings use the admitted fixture owner, camera-relative static root and submitted draw accounting, and are hidden in the workshop. No added save identity, collider, GPU ABI field or raised limit was found in these integrations.
- **D41/D42 guidance:** the native formatter samples authoritative delivery eligibility and actual command guards. Pending installation/checkpoint/recovery and current save failures outrank historical delivery/power. The browser card revalidates the same existing action before forwarding once; it does not create gameplay authority, preserves focus on unchanged refresh, and suppresses actions for closed admission, pending work, pause, failure and workshop. Its simpler instructions do not grant delivery permission from distance.
- **Native power acceptance:** the delivery driver resumes, performs a real berth recovery, reaches actual installation availability, sends K, then waits for harbor durability, no installation pending and paused state. It separately requires the powered HUD, whose invariant demands installed plus durable harbor state, and checks the resulting archive tick and preserved ownership. An already paused delivery cannot satisfy the power wait.
- **F9/surface:** the single existing F9 binding precedes the Cove/workshop early returns. Named surface Timeout skips before frame tickets; recovery logging and move/shutdown flag handling remain intact. Other error classification and strict driver scanning are preserved.

## Review limits

Source review only: no tests, builds, GPU/application runs, browser/native views or images were performed. This document does not claim that the running native journey, rebuilt browser acceptance, mandatory suite, publication or a broader plan gate passed. Root owns those results. Source-level CSS wrapping review is not a measured viewport result.

## Reviewed live source identities

- `src/app/application.cpp` — SHA-256 `5afdd3007e500e85fa7654652b9bf5f180358be0fa57be4294743033bfb840d1`
- `src/game/expedition/cove_rigid_roots.cpp` — SHA-256 `dd0949937e05b1bef70028563691675c1dc7185f2bab5833e449672f2406dd4f`
- `src/game/expedition/cove_rigid_roots.hpp` — SHA-256 `9dd26b21bb4a8e3d372d1ea6fb270241d85e7e5f02a8f3d19a11fc6cc8aa0d83`
- `src/render/cove_recovery_guidance.hpp` — SHA-256 `52bf741dbeb25e8034f3a958121e848ce9ffb23aa985f26c80f21dfabb9fca7d`
- `web/salvage_preview.js` — SHA-256 `df58cd40eca80d859029ccd11b99fc219e8cb442a041504ca23e741abffdf25c`
- `scripts/validate_native_cove_delivery.py` — SHA-256 `bef18ffa6bfbd728b6c749854a78686ab568f396cb2d0d6882550f3ce639367f`
