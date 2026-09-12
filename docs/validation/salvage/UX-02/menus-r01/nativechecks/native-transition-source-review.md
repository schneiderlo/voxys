# Native world transition drain review

The retained native UI continuation r03 exposed a real lifecycle defect: New was accepted, but logical Preview Empty requested process exit before all authored owners drained. The native host correctly refused the undrained transition, and shutdown logged the abandoned-resource warning. This attempt remains failed.

The reviewed correction factors the existing complete JSON drain predicate into `Application::coveAssetsDraining()`. It covers fixture retirement, all boat roots, scenery/environment/cargo, harbor, pending authored-shape operations, and the submitted event frontier. JSON, timeout, the per-frame exit gate, and `coveWorldTransitionDrained()` now use that same predicate. One-time logical Empty cleanup is retained; `updateCoveBoat()` keeps polling death evidence and retiring owners on later frames. The native host still requires a completed transition before transferring its staged target.

No actionable finding in this bounded independent source review. No code was changed or tests, GPU work, or images run for the review. Actual r06 New/Load/quit/restart evidence is recorded separately; this review does not claim its result.

Reviewed source SHA-256:

- `src/app/application.cpp`: `348e3791980fc7a47f74b6302d8632ad0e7d7f62d4053ae697c5faee0ac25bbb`
- `src/app/application.hpp`: `e1ab699188b0c866e6d1497e2997e22acbe1f56dee91cb661ec846acaab20086`
- `src/engine/platform/native/cove_saves.cpp`: `1a7610a7bbc5cb3769c0767cfba868fcebd56549f3c473004fc1014e8d0cb440`
- `src/engine/platform/native/entry.cpp`: `00ecf89ee4f2ca4058f6a4b30cbccecb19eb1ce8e9f2dfc8b757f6f20455b890`
