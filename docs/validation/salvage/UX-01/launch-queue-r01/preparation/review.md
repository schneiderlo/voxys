# D43 independent source review

Reviewed `helper.patch` and `application.patch`, including the isolated copies of `src/game/expedition/cove_rigid_roots.hpp`, `src/game/expedition/cove_rigid_roots.cpp`, `src/game/BUILD`, `src/app/application.cpp`, and the two new cases in `tests/test_cove_player.cpp`.

No actionable findings.

- Busy/NotReady retains the compiled pending candidate and untouched remaining payloads. Only roots without accepted handles are retried; accepted roots are not uploaded twice.
- Every returned valid handle is stored before error classification, including the real backend edge that can return an inserted handle together with GpuFailure.
- Existing bindings and payload counts are validated before taking another owner. Uploading never qualifies as Ready, including after Application clears fully moved payload storage.
- Terminal errors retain partial handles in the canceled candidate. The existing owner retires accepted handles and keeps them until Missing.
- Existing joined scheduled/encoded/completed staging checks, atomic parent replacement and parent-death observation requirements remain unchanged. No body, debit, accepted design, GPU limit, or save schema is changed by the helper.
- The two scripted-resource CPU cases check preserved bytes/storage, no duplicate uploads, delayed readiness, malformed preparation, Capacity refusal, valid-handle GpuFailure and explicit Retiring-to-Missing ownership. Their comments correctly distinguish scripted protocol evidence from real GPU completion.

This reviewer performed source reads only. No builds, tests, GPU/device runs, application journeys, screenshots or images were run. Root owns actual focused checks, uncapped browser launch acceptance, combined native evidence and the mandatory final suite.
